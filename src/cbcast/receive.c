#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include "vector_clock.h"

enum CausalityType {
  CAUSALITY_ERROR = -1,
  CAUSALITY_DELIVER,
  CAUSALITY_HOLD,
};
typedef enum CausalityType causality_t;

// ************** Function Declaration ***************
//
causality_t vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                               uint64_t clock);

void retransmit_msg_missing_ack(cbcast_t *cbc);

char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                          socklen_t *addr_len, size_t *recv_len);

Result *create_received_message(cbcast_msg_t *msg, uint16_t sender_pid,
                                uint16_t sender_idx);

cbcast_peer_t *find_sender(const cbcast_t *cbc,
                           const struct sockaddr_in *sender_ipv4);

void ack_msg(const cbcast_t *cbc, const cbcast_peer_t *peer,
             const uint16_t ack_clock);

void verify_held_msg_causality(cbcast_t *cbc);

void process_ack(cbcast_t *cbc, cbcast_received_msg_t *rcvd);

void ask_for_retransmissions(const cbcast_t *cbc, const cbcast_peer_t *peer);

void process_retransmission_req(cbcast_t *cbc, cbcast_received_msg_t *ret_req);

cbcast_received_msg_t *process_data_msg(cbcast_t *cbc,
                                        cbcast_received_msg_t *rcvd);

cbcast_received_msg_t *deliver_and_request_retransmission(cbcast_t *cbc);

void check_and_request_retransmissions(cbcast_t *cbc);

// ************** Public Functions ***************
//
void cbc_received_message_free(cbcast_received_msg_t *msg) {
  if (!msg) {
    return;
  }
  if (msg->message) {
    cbc_msg_free(msg->message);
  }
  free(msg);
}

cbcast_received_msg_t *cbc_receive(cbcast_t *cbc) {
  if (!cbc)
    return NULL; // Invalid input

  struct sockaddr sender_addr;
  memset(&sender_addr, 0, sizeof(sender_addr)); // Ensure full initialization

  size_t recv_len = 0;
  socklen_t addr_len = sizeof(struct sockaddr_in);

  // Step 1: Receive the raw message
  char *buffer = receive_raw_message(cbc, &sender_addr, &addr_len, &recv_len);
  if (!buffer) // No data or error
    return deliver_and_request_retransmission(cbc);

  // Step 1: Identify the sender
  struct sockaddr_in sender_ipv4 = *(struct sockaddr_in *)&sender_addr;
  cbcast_peer_t *sender = find_sender(cbc, &sender_ipv4);
  if (!sender) {
    free(buffer);
    return deliver_and_request_retransmission(cbc);
  }

  // Step 3: Deserialize the received message
  Result *serialize_res = cbc_msg_deserialize(buffer);
  free(buffer);
  if (result_is_err(serialize_res)) {
    result_free(serialize_res);
    return deliver_and_request_retransmission(cbc);
  }

  cbcast_received_msg_t *received = result_unwrap(create_received_message(
      result_unwrap(serialize_res), sender->pid, sender->pos));

  /* printf("[cbc_receive] cbc pid %lu received message type %d with clock %d "
   */
  /*        "from peer %lu\n", */
  /*        cbc->pid, received->message->header->kind, */
  /*        received->message->header->clock, sender->pid); */

  switch (received->message->header->kind) {
  case CBC_DATA:
    return process_data_msg(cbc, received);

  case CBC_ACK:
    process_ack(cbc, received);
    return deliver_and_request_retransmission(cbc);

  case CBC_RETRANSMIT_REQ:
    process_retransmission_req(cbc, received);
    return deliver_and_request_retransmission(cbc);

  case CBC_RETRANSMIT:
    return RESULT_UNREACHABLE;

  case CBC_HEARTBEAT:
    arrput(cbc->delivery_queue, received);
    return deliver_and_request_retransmission(cbc);

  default:
    return RESULT_UNREACHABLE;
  }
}

// ************** Private Functions ***************
//
causality_t vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                               uint64_t clock) {
  if (pid >= vclock->len) {
    return CAUSALITY_ERROR;
  }

  if (vclock->clock[pid] + 1 == clock) {
    return CAUSALITY_DELIVER;
  }

  if (vclock->clock[pid] + 1 < clock) {
    return CAUSALITY_HOLD;
  }

  return CAUSALITY_ERROR;
}

Result *create_received_message(cbcast_msg_t *msg, uint16_t sender_pid,
                                uint16_t sender_idx) {
  cbcast_received_msg_t *received = calloc(1, sizeof(cbcast_received_msg_t));
  if (!received) {
    return result_new_err("[create_received_message] Memory allocation failed");
  }
  received->message = msg;
  received->sender_pid = sender_pid;
  received->sender_idx = sender_idx;
  return result_new_ok(received);
}

// Receive raw message from the socket
char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                          socklen_t *addr_len, size_t *recv_len) {
  if (!cbc || !sender_addr || !addr_len || !recv_len)
    return NULL;

  size_t buffer_size = 1024; // Adjust size as needed
  char *buffer = calloc(buffer_size, sizeof(char));
  if (!buffer) {
    fprintf(stderr, "[receive_raw_message] Memory allocation failed");
    return NULL;
  }

  *recv_len = recvfrom(cbc->socket_fd, buffer, buffer_size, MSG_DONTWAIT,
                       sender_addr, addr_len);
  if (*recv_len <= 0 || *recv_len > buffer_size || !sender_addr || !addr_len) {
    free(buffer);
    return NULL; // No data or error
  }

  if (*recv_len < sizeof(cbcast_msg_hdr_t)) {
    fprintf(stderr, "[receive_raw_message] Message too short\n");
    free(buffer);
    return NULL;
  }

  return buffer;
}

// Find sender from the list of known peers
cbcast_peer_t *find_sender(const cbcast_t *cbc,
                           const struct sockaddr_in *sender_ipv4) {
  if (!sender_ipv4 || !cbc) {
    fprintf(stderr, "[cbc_rcv] Null sender address\n");
    return NULL;
  }

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, sender_ipv4, sizeof(struct sockaddr_in)) ==
        0) {
      cbc->peers[i]->pos = i;
      return cbc->peers[i];
    }
  }

  char ipv4[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &sender_ipv4->sin_addr.s_addr, ipv4, INET_ADDRSTRLEN);
  fprintf(stderr, "[cbc_rcv] Unknown sender %s:%d\n", ipv4,
          ntohs(sender_ipv4->sin_port));

  return NULL; // Unknown sender
}

void ack_msg(const cbcast_t *cbc, const cbcast_peer_t *peer,
             const uint16_t ack_clock) {
  // Step 1: Ack the received message
  cbcast_msg_t *ack_msg = result_unwrap(cbc_msg_create(CBC_ACK, NULL, 0));
  ack_msg->header->clock = ack_clock;

  size_t ack_msg_size = 0;
  char *ack_msg_bytes = cbc_msg_serialize(ack_msg, &ack_msg_size);

  char err_msg[1024];
  sprintf(err_msg,
          "[process_data_msg] cbc pid %lu Failed to send ACK to peer pid %lu "
          "idx %zu for "
          "message with clock %d.\n",
          cbc->pid, peer->pid, peer->pos, ack_clock);

  result_expect(
      cbc_send_to_peer(cbc, peer, ack_msg_bytes, ack_msg_size, MSG_CONFIRM),
      err_msg);

  free(ack_msg_bytes);
  cbc_msg_free(ack_msg);
}

void verify_held_msg_causality(cbcast_t *cbc) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf);) {
    cbcast_received_msg_t *held_msg = cbc->held_buf[i];
    if (vc_check_causality(cbc->vclock, held_msg->sender_pid,
                           held_msg->message->header->clock) == 0) {
      arrput(cbc->delivery_queue, held_msg);
      (void)vc_inc(cbc->vclock, held_msg->sender_pid);

      // Remove from held buffer
      arrdel(cbc->held_buf, i);
    } else {
      i++; // Only increment if not removing
    }
  }
}

cbcast_received_msg_t *deliver_and_request_retransmission(cbcast_t *cbc) {
  check_and_request_retransmissions(cbc);
  retransmit_msg_missing_ack(cbc);

  // Return message in delivery queue, if any
  if (arrlen(cbc->delivery_queue) <= 0) {
    return NULL; // No message ready for delivery
  }

  cbcast_received_msg_t *delivered_msg = cbc->delivery_queue[0];
  arrdel(cbc->delivery_queue, 0); // Remove from queue
  return delivered_msg;
}

void process_ack(cbcast_t *cbc, cbcast_received_msg_t *ack) {
  if (ack->message->header->kind != CBC_ACK) {
    RESULT_UNREACHABLE;
  }

  int msg_index = -1;

  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_buf); i++) {
    cbcast_sent_msg_t *sent_msg = cbc->sent_buf[i];
    if (sent_msg->message->header->clock == ack->message->header->clock) {
      msg_index = i;
      break;
    }
  }

  if (msg_index == -1) {
    fprintf(stderr, "[process_ack] ACK for unknown message\n");
    return;
  }

  cbcast_sent_msg_t *acked_msg = cbc->sent_buf[msg_index];
  acked_msg->confirms[ack->sender_idx] = 1;
  /* printf("[process_ack] cbc pid: %lu ACK received from peer %hu for message "
   */
  /*        "with clock %d\n", */
  /*        cbc->pid, ack->sender_pid, acked_msg->message->header->clock); */

  // Check if all peers have ACKed the message
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (!acked_msg->confirms[i]) {
      return; // Not all ACKs received
    }
  }

  // debug print
  /* printf( */
  /*     "[process_ack] cbc pid: %lu All ACKs received for message with clock
   * %d. " */
  /*     "Removing from sent buffer\n", */
  /*     cbc->pid, acked_msg->message->header->clock); */

  // All ACKs received, remove from sent buffer
  arrdelswap(cbc->sent_buf, msg_index);
  cbc_msg_free(acked_msg->message);
  free(acked_msg->confirms);
  free(acked_msg);

  cbc_received_message_free(ack);
}

void ask_for_retransmissions(const cbcast_t *cbc, const cbcast_peer_t *peer) {
  if (!arrlen(cbc->held_buf)) {
    return;
  }

  cbcast_msg_t *retransmit_msg =
      result_unwrap(cbc_msg_create(CBC_RETRANSMIT_REQ, NULL, 0));

  retransmit_msg->header->clock = cbc->vclock->clock[peer->pid] + 1;

  /* printf("[ask_for_retransmissions] cbc pid %lu Requesting retransmission for
   * " */
  /*        "clock %d from peer %lu\n", */
  /*        cbc->pid, retransmit_msg->header->clock, peer->pid); */

  size_t retransmit_size = 0;
  char *retransmit_bytes = cbc_msg_serialize(retransmit_msg, &retransmit_size);
  result_unwrap(
      cbc_send_to_peer(cbc, peer, retransmit_bytes, retransmit_size, 0));

  free(retransmit_bytes);
  cbc_msg_free(retransmit_msg);
}

cbcast_received_msg_t *process_data_msg(cbcast_t *cbc,
                                        cbcast_received_msg_t *rcvd) {

  ack_msg(cbc, cbc->peers[rcvd->sender_idx], rcvd->message->header->clock);

  // Step 2: Assert Message Causality;
  causality_t causality = vc_check_causality(cbc->vclock, rcvd->sender_pid,
                                             rcvd->message->header->clock);

  // Step 3: Process the message based on causality
  switch (causality) {
  case CAUSALITY_DELIVER:
    arrput(cbc->delivery_queue, rcvd);           // Add to delivery queue
    (void)vc_inc(cbc->vclock, rcvd->sender_pid); // Update vector clock
    verify_held_msg_causality(cbc); // Process existing held messages
    break;

  case CAUSALITY_HOLD:
    // check if the message is already in the held buffer to avoid duplicates
    for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf); i++) {
      if (cbc->held_buf[i]->message->header->clock ==
              rcvd->message->header->clock &&
          cbc->held_buf[i]->sender_pid == rcvd->sender_pid) {
        printf("[process_data_msg] cbc pid %lu Duplicate message received "
               "from peer %hu with clock %d\n",
               cbc->pid, rcvd->sender_pid, rcvd->message->header->clock);
        break;
      }
    }

    arrput(cbc->held_buf, rcvd); // Hold the message
    break;

  case CAUSALITY_ERROR:
    // Ignore. It may be a retransmission
    /* fprintf(stderr, */
    /*         "[process_data_msg] Causality error cbc pid %lu sender %hu " */
    /*         "received clock %d expected clock %lu\n", */
    /*         cbc->pid, rcvd->sender_pid, rcvd->message->header->clock, */
    /*         cbc->vclock->clock[rcvd->sender_pid] + 1); */
    break;

  default:
    return RESULT_UNREACHABLE;
  };

  return deliver_and_request_retransmission(cbc);
}

void process_retransmission_req(cbcast_t *cbc, cbcast_received_msg_t *ret_req) {
  // This should never happen
  if (!cbc || !ret_req ||
      ret_req->message->header->kind != CBC_RETRANSMIT_REQ) {
    return (void)RESULT_UNREACHABLE;
  }

  // Step 1: Find the requested message in the sent buffer
  cbcast_sent_msg_t *sent_msg = NULL;
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_buf); i++) {
    uint16_t sent_clock = cbc->sent_buf[i]->message->header->clock;
    if (ret_req->message->header->clock == sent_clock) {
      sent_msg = cbc->sent_buf[i];
      break;
    }
  }

  if (!sent_msg) {
    /* printf("[process_retransmission_req] cbc pid %lu No message found for "
     */
    /*        "retransmission request with clock %d\n", */
    /*        cbc->pid, ret_req->message->header->clock); */
    return (void)RESULT_UNREACHABLE;
  }

  cbcast_peer_t *sender = cbc->peers[ret_req->sender_idx];
  size_t retransmit_size = 0;
  char *retransmit_bytes =
      cbc_msg_serialize(sent_msg->message, &retransmit_size);

  result_expect(cbc_send_to_peer(cbc, sender, retransmit_bytes, retransmit_size,
                                 MSG_CONFIRM),
                "Peer should be valid");

  free(retransmit_bytes);
  cbc_received_message_free(ret_req);
}

void check_and_request_retransmissions(cbcast_t *cbc) {
  if (!cbc || !arrlen(cbc->held_buf)) {
    return; // Nothing to process
  }

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];

    // Check if there are held messages for this peer
    bool has_held_messages = false;
    for (size_t j = 0; j < (size_t)arrlen(cbc->held_buf); j++) {
      if (cbc->held_buf[j]->sender_pid == peer->pid) {
        has_held_messages = true;
        break;
      }
    }

    // If held messages exist, request retransmissions
    if (has_held_messages) {
      ask_for_retransmissions(cbc, peer);
    }
  }
}

void retransmit_msg_missing_ack(cbcast_t *cbc) {
  if (arrlen(cbc->sent_buf) <= 10) {
    return;
  }

  cbcast_sent_msg_t *msg = cbc->sent_buf[0];
  size_t msg_size = 0;
  char *msg_bytes = cbc_msg_serialize(msg->message, &msg_size);

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (!msg->confirms[i]) {
      cbc_send_to_peer(cbc, cbc->peers[i], msg_bytes, msg_size, 0);
    }
  }

  free(msg_bytes);
}
