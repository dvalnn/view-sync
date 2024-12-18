#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include "vector_clock.h"

// ************** Function Declaration ***************
//
static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len);
static Result *create_received_message(cbcast_msg_t *msg, uint16_t sender_pid);

static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_addr);

static void process_held_msgs(cbcast_t *cbc);

static cbcast_received_msg_t *process_data_msg(cbcast_t *cbc,
                                               cbcast_received_msg_t *rcvd);

static cbcast_received_msg_t *deliver_message(cbcast_t *cbc);

// ************** Inline Functions ***************
//
static inline int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                                     uint64_t clock) {
  return (clock == vclock->clock[pid] + 1) ? 0 : -1;
}

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
  socklen_t addr_len = sizeof(sender_addr);
  size_t recv_len = 0;

  // Step 1: Receive the raw message
  char *buffer = receive_raw_message(cbc, &sender_addr, &addr_len, &recv_len);
  if (!buffer) // No data or error
    return deliver_message(cbc);

  // Step 1: Identify the sender
  struct sockaddr_in sender_ipv4 = *(struct sockaddr_in *)&sender_addr;
  cbcast_peer_t *sender = find_sender(cbc, &sender_ipv4);
  if (!sender) {
    return deliver_message(cbc);
  }

  // Step 3: Deserialize the received message
  Result *serialize_res = cbc_msg_deserialize(buffer);
  free(buffer);
  if (result_is_err(serialize_res)) {
    result_free(serialize_res);
    return deliver_message(cbc);
  }
  cbcast_received_msg_t *received = result_unwrap(
      create_received_message(result_unwrap(serialize_res), sender->pid));

  switch (received->message->header->kind) {
  case CBC_HEARTBEAT:
    arrput(cbc->delivery_queue, received);
    return deliver_message(cbc);

  case CBC_RETRANSMIT:
    RESULT_UNIMPLEMENTED;
    return NULL;

  case CBC_DATA:
    return process_data_msg(cbc, received);

  default:
    RESULT_UNREACHABLE;
    return NULL;
  }
}

// ************** Private Functions ***************
//
static Result *create_received_message(cbcast_msg_t *msg, uint16_t sender_pid) {
  cbcast_received_msg_t *received = calloc(1, sizeof(cbcast_received_msg_t));
  if (!received) {
    return result_new_err("[create_received_message] Memory allocation failed");
  }
  received->message = msg;
  received->sender_pid = sender_pid;
  return result_new_ok(received);
}

// Receive raw message from the socket
static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len) {
  if (!cbc || !sender_addr || !addr_len || !recv_len)
    return NULL;

  size_t buffer_size = 1024; // Adjust size as needed
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("[receive_raw_message] Memory allocation failed");
    return NULL;
  }

  *recv_len = recvfrom(cbc->socket_fd, buffer, buffer_size, MSG_DONTWAIT,
                       sender_addr, addr_len);
  if (*recv_len <= 0) {
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
static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_ipv4) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, sender_ipv4, sizeof(struct sockaddr_in)) ==
        0) {
      return cbc->peers[i];
    }
  }

  char ipv4[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sender_ipv4->sin_addr.s_addr, ipv4, INET_ADDRSTRLEN);
  fprintf(stderr, "[cbc_rcv] Unknown sender %s:%d\n", ipv4,
          ntohs(sender_ipv4->sin_port));

  return NULL; // Unknown sender
}

static void process_held_msgs(cbcast_t *cbc) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf);) {
    cbcast_received_msg_t *held_msg = cbc->held_buf[i];
    if (vc_check_causality(cbc->vclock, held_msg->sender_pid,
                           held_msg->message->header->clock) == 0) {
      arrput(cbc->delivery_queue, held_msg);
      vc_inc(cbc->vclock, held_msg->sender_pid);

      // Remove from held buffer
      arrdel(cbc->held_buf, i);
    } else {
      i++; // Only increment if not removing
    }
  }
}

static cbcast_received_msg_t *process_data_msg(cbcast_t *cbc,
                                               cbcast_received_msg_t *rcvd) {
  if (vc_check_causality(cbc->vclock, rcvd->sender_pid,
                         rcvd->message->header->clock) != 0) {
    arrput(cbc->held_buf, rcvd); // Hold the message
    return deliver_message(cbc);
  }

  arrput(cbc->delivery_queue, rcvd);     // Add to delivery queue
  vc_inc(cbc->vclock, rcvd->sender_pid); // Update vector clock
  process_held_msgs(cbc);                // Process existing held messages
  return deliver_message(cbc);
}

static cbcast_received_msg_t *deliver_message(cbcast_t *cbc) {
  // Return message in delivery queue, if any
  if (arrlen(cbc->delivery_queue) > 0) {
    cbcast_received_msg_t *delivered_msg = cbc->delivery_queue[0];
    arrdel(cbc->delivery_queue, 0); // Remove from queue
    return delivered_msg;
  }

  return NULL; // No message ready for delivery
}
