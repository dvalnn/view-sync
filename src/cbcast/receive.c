#include "cbcast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/stb_ds.h"

void ack_received_message(cbcast_t *cbc, uint16_t ack_clock,
                          uint16_t sender_pid);

enum CausalityType {
  CAUSALITY_ERROR = -1,
  CAUSALITY_DELIVER,
  CAUSALITY_HOLD,
};
typedef enum CausalityType causality_t;

causality_t check_msg_causality(vector_clock_t *vclock, uint64_t pid,
                                uint64_t clock);

void handle_data_msg(cbcast_t *cbc, cbcast_msg_t *msg, uint16_t sender_pid);

void register_ack(cbcast_t *cbc, cbcast_msg_t *msg, uint16_t sender_pid);

void request_retransmission(cbcast_t *cbc, uint16_t sender_pid);

void queue_retransmission(cbcast_t *cbc, cbcast_msg_t *msg,
                          uint16_t sender_pid);

// ************** Function Definition ***************
cbcast_received_msg_t *cbc_receive(cbcast_t *cbc) {
  if (!cbc) {
    return NULL;
  }

  pthread_mutex_lock(&cbc->recv_lock);
  cbcast_received_msg_t *msg =
      arrlen(cbc->delivery_queue) > 0 ? cbc->delivery_queue[0] : NULL;

  if (msg) {
    arrdel(cbc->delivery_queue, 0);
  }

  pthread_mutex_unlock(&cbc->recv_lock);

  return msg;
}

void *cbc_recv_thread(void *arg) {
  cbcast_t *cbc = (cbcast_t *)arg;
  char header_buffer[sizeof(cbcast_msg_hdr_t)];
  char *full_msg_buffer = NULL;

  struct sockaddr sender_addr;
  for (;;) {
    memset(&sender_addr, 0, sizeof(sender_addr));
    socklen_t addr_len = sizeof(struct sockaddr_in);

    uint64_t recv_len =
        (uint64_t)recvfrom(cbc->socket_fd, header_buffer, sizeof(header_buffer),
                           MSG_PEEK, &sender_addr, &addr_len);

    if (recv_len < sizeof(cbcast_msg_hdr_t)) {
      continue; // Not enough data
    }

    cbcast_msg_hdr_t *header = (cbcast_msg_hdr_t *)header_buffer;
    size_t full_msg_size = sizeof(cbcast_msg_hdr_t) + header->len;
    full_msg_buffer = calloc(full_msg_size, sizeof(char));
    if (!full_msg_buffer) {
      fprintf(stderr, "[cbc_recv] Memory allocation failed\n");
      continue;
    }

    recv_len = (uint64_t)recvfrom(cbc->socket_fd, full_msg_buffer,
                                  full_msg_size, 0, &sender_addr, &addr_len);
    if (recv_len != full_msg_size) {
      free(full_msg_buffer);
      continue;
    }

    uint16_t sender_pid =
        cbc_peer_find_by_addr(cbc, (struct sockaddr_in *)&sender_addr);
    if (sender_pid == UINT16_MAX) {
      free(full_msg_buffer);
      continue;
    }

    Result *msg_deserialize_raw = cbc_msg_deserialize(full_msg_buffer);
    if (result_is_err(msg_deserialize_raw)) {
      free(full_msg_buffer);
      continue;
    }
    cbcast_msg_t *msg = result_expect(msg_deserialize_raw, "unreachable");

    switch (msg->header->kind) {
    case CBC_DATA:
    case CBC_RETRANSMIT:
      ack_received_message(cbc, msg->header->clock, sender_pid);
      handle_data_msg(cbc, msg, sender_pid);
      break;

    case CBC_ACK:
      pthread_mutex_lock(&cbc->send_lock);
      { register_ack(cbc, msg, sender_pid); }
      pthread_mutex_unlock(&cbc->send_lock);

      cbc_msg_free(msg);
      break;

    case CBC_RETRANSMIT_REQ:
      queue_retransmission(cbc, msg, sender_pid);
      cbc_msg_free(msg);
      break;

    case CBC_HEARTBEAT:
      return RESULT_UNREACHABLE;
    }
  }

  return NULL;
}

void ack_received_message(cbcast_t *cbc, uint16_t ack_clock,
                          uint16_t sender_pid) {
  if (!cbc || ack_clock == UINT16_MAX) {
    return (void)RESULT_UNREACHABLE;
  }

  cbcast_msg_t *ack_msg = result_unwrap(cbc_msg_create(CBC_ACK, NULL, 0));
  ack_msg->header->clock = ack_clock;

  struct sockaddr_in *sender_addr = cbc_peer_get_addr_copy(cbc, sender_pid);
  if (!sender_addr) {
    fprintf(stderr, "[queue_new_ack] Invalid sender pid\n");
    return;
  }

  cbcast_outgoing_msg_t *outgoing =
      result_unwrap(cbc_outgoing_msg_create(ack_msg, sender_addr));

  pthread_mutex_lock(&cbc->send_lock);
  arrput(cbc->send_queue, outgoing);
  pthread_mutex_unlock(&cbc->send_lock);

  pthread_cond_signal(&cbc->send_cond);
}

causality_t check_msg_causality(vector_clock_t *vclock, uint64_t pid,
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

void request_retransmission(cbcast_t *cbc, uint16_t sender_pid) {
  if (!cbc || sender_pid == UINT16_MAX) {
    return (void)RESULT_UNREACHABLE;
  }

  pthread_mutex_lock(&cbc->vclock->mtx);
  uint64_t expected_clock = cbc->vclock->clock[sender_pid] + 1;
  pthread_mutex_unlock(&cbc->vclock->mtx);

  cbcast_msg_t *retransmit_req =
      result_unwrap(cbc_msg_create(CBC_RETRANSMIT_REQ, NULL, 0));
  retransmit_req->header->clock = expected_clock;

  struct sockaddr_in *sender_addr = cbc_peer_get_addr_copy(cbc, sender_pid);
  if (!sender_addr) {
    fprintf(stderr, "[request_retransmission] Invalid sender pid\n");
    return;
  }

  cbcast_outgoing_msg_t *outgoing =
      result_unwrap(cbc_outgoing_msg_create(retransmit_req, sender_addr));

  pthread_mutex_lock(&cbc->send_lock);
  arrput(cbc->send_queue, outgoing);
  pthread_mutex_unlock(&cbc->send_lock);

  pthread_cond_signal(&cbc->send_cond);
};

void handle_data_msg(cbcast_t *cbc, cbcast_msg_t *msg, uint16_t sender_pid) {
  if (!cbc || !msg) {
    fprintf(stderr, "[data_deliver_or_hold] Null cbcast_t or cbcast_msg_t\n");
    return;
  }

  pthread_mutex_lock(&cbc->vclock->mtx);
  causality_t causality =
      check_msg_causality(cbc->vclock, sender_pid, msg->header->clock);
  pthread_mutex_unlock(&cbc->vclock->mtx);

  cbcast_received_msg_t *rcvd = result_expect(
      cbc_received_msg_create(msg, sender_pid),
      "cbcast_msg_t should be convertible to cbcast_received_msg_t");

  switch (causality) {
  case CAUSALITY_ERROR:
    if (msg->header->kind == CBC_RETRANSMIT) {
      cbc_received_msg_free(rcvd);
      return;
    }
    // should not happen
    return (void)RESULT_UNREACHABLE;

  case CAUSALITY_DELIVER:
    pthread_mutex_lock(&cbc->recv_lock);
    {
      pthread_mutex_lock(&cbc->vclock->mtx);
      // increment the vector clock
      { cbc->vclock->clock[sender_pid]++; }
      pthread_mutex_unlock(&cbc->vclock->mtx);

      arrput(cbc->delivery_queue, rcvd);
    }
    pthread_mutex_unlock(&cbc->recv_lock);
    break;

  case CAUSALITY_HOLD:
    pthread_mutex_lock(&cbc->recv_lock);
    arrput(cbc->held_msg_buffer, rcvd);
    pthread_mutex_unlock(&cbc->recv_lock);

    request_retransmission(cbc, sender_pid);
    break;
  }
}

void register_ack(cbcast_t *cbc, cbcast_msg_t *ack, uint16_t sender_pid) {
  if (!cbc || !ack) {
    fprintf(stderr, "[register_ack] Null cbcast_t or cbcast_msg_t\n");
    return;
  }

  if (ack->header->kind != CBC_ACK) {
    return (void)RESULT_UNREACHABLE;
  }

  // Find the message in the sent buffer and ack it
  size_t msg_idx = arrlen(cbc->sent_msg_buffer);

  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i];
    if (sent_msg->message->header->clock == ack->header->clock) {
      msg_idx = i;
      break;
    }
  }

  if (msg_idx == (size_t)arrlen(cbc->sent_msg_buffer)) {
    // Ignore. May be a retransmission of an ack
    return;
  }

  cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[msg_idx];
  sent_msg->ack_bitmap |= 1 << sender_pid;
  printf(
      "[register_ack] cbc pid %lu received ACK from peer %d for message %d\n",
      cbc->pid, sender_pid, ack->header->clock);

  if (sent_msg->ack_bitmap == sent_msg->ack_target) {
    arrdel(cbc->sent_msg_buffer, msg_idx);
    printf("[register_ack] cbc pid %lu message %d fully acked\n", cbc->pid,
           ack->header->clock);
  }
}

void queue_retransmission(cbcast_t *cbc, cbcast_msg_t *msg,
                          uint16_t sender_pid) {

  if (!cbc || !msg || sender_pid == UINT16_MAX) {
    return (void)RESULT_UNREACHABLE;
  }

  cbcast_msg_t *retransmit_msg = NULL;

  pthread_mutex_lock(&cbc->send_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    if (cbc->sent_msg_buffer[i]->message->header->clock == msg->header->clock) {
      cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i];
      retransmit_msg = result_unwrap(
          cbc_msg_create(CBC_RETRANSMIT, sent_msg->message->payload,
                         sent_msg->message->header->len));

      break;
    }
  }
  pthread_mutex_unlock(&cbc->send_lock);

  if (!retransmit_msg) {
    fprintf(stderr,
            "[queue_retransmission] No message found for retransmission "
            "request with clock %d\n",
            msg->header->clock);
    return;
  }

  struct sockaddr_in *sender_addr = cbc_peer_get_addr_copy(cbc, sender_pid);

  cbcast_outgoing_msg_t *outgoing =
      result_unwrap(cbc_outgoing_msg_create(retransmit_msg, sender_addr));

  pthread_mutex_lock(&cbc->send_lock);
  arrput(cbc->send_queue, outgoing);
  pthread_mutex_unlock(&cbc->send_lock);

  pthread_cond_signal(&cbc->send_cond);
}
