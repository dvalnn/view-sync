#include "cbcast.h"
#include "lib/stb_ds.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

static int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp);
static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len);
static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_addr);
static void process_held_messages(cbcast_t *cbc);

static void hold_message(cbcast_t *cbc, cbcast_msg_t *msg, uint16_t sender_pid);

// Main Function
char *cbc_rcv(cbcast_t *cbc) {
  if (!cbc) {
    return NULL; // Invalid input
  }

  struct sockaddr sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  size_t recv_len = 0;

  // Step 1: Receive the raw message
  char *buffer = receive_raw_message(cbc, &sender_addr, &addr_len, &recv_len);
  if (!buffer) {
    return NULL; // No data or error in receiving
  }

  Result *serialize_res = cbc_msg_deserialize(buffer);
  free(buffer);
  if (result_is_err(serialize_res)) {
    result_free(serialize_res);
    goto exit;
  }

  cbcast_msg_t *received = result_unwrap(serialize_res);

  // Step 3: Identify the sender
  struct sockaddr_in sender_ipv4 = *(struct sockaddr_in *)&sender_addr;
  cbcast_peer_t *sender = find_sender(cbc, &sender_ipv4);
  if (!sender) {
    char ipv4[INET_ADDRSTRLEN];
    int port = ntohs(sender_ipv4.sin_port);
    inet_ntop(AF_INET, &sender_ipv4.sin_addr.s_addr, ipv4, INET_ADDRSTRLEN);

    fprintf(
        stderr,
        "[cbc_rcv] Cbc pid %lu received message from unknown sender %s:%d\n",
        cbc->pid, ipv4, port);
    goto exit;
  }

  // Step 4: Handle the message
  int causal_result =
      vc_check_causality(cbc->vclock, sender->pid, received->header->clock);

  if (causal_result == 0) {
    printf("cbc pid %lu received message %hu from peer %lu\n", cbc->pid,
           received->header->clock, sender->pid);
    arrput(cbc->delivery_queue,
           strdup(received->payload)); // Copy to delivery queue
    vc_inc(cbc->vclock, sender->pid);  // Update vector clock
    cbc_msg_free(received);

    process_held_messages(cbc);
  } else {
    // Hold received message
    hold_message(cbc, received, sender->pid);
  }

exit:
  // Step 6: Return a message from the delivery queue if available
  printf("cbc pid %lu delivery_queue len is %td\n", cbc->pid,
         arrlen(cbc->delivery_queue));
  if (arrlen(cbc->delivery_queue) > 0) {
    char *delivered_msg = cbc->delivery_queue[0]; // Get the first message
    arrdel(cbc->delivery_queue, 0);               // Remove from delivery queue

    printf("cbc pid %lu delivered message: len--\n", cbc->pid);
    return delivered_msg;
  }

  return NULL; // No message ready for delivery
}

// ************************************************
// ************************************************
// ************** Private Functions ***************
// ************************************************
// ************************************************

static int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp) {
  uint64_t current = vclock->clock[pid];
  return (timestamp == current + 1) ? 0 : -1; // 0 = deliverable, -1 = hold
}

static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len) {
  if (!cbc || !sender_addr || !addr_len || !recv_len) {
    return NULL;
  }

  size_t buffer_size = 1024; // Adjust size based on expected message sizes
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    fprintf(stderr, "[receive_raw_message] Failed to allocate buffer\n");
    return NULL;
  }

  *recv_len = recvfrom(cbc->socket_fd, buffer, buffer_size, MSG_DONTWAIT,
                       sender_addr, addr_len);
  if (*recv_len <= 0) {
    free(buffer);
    return NULL; // No data or error in receiving
  }

  if (*recv_len < sizeof(cbcast_msg_hdr_t)) {
    fprintf(stderr, "[receive_raw_message] msg too short\n");
    return NULL; // No data or error in receiving
  }

  return buffer;
}

static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_addr) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, sender_addr, sizeof(struct sockaddr_in)) ==
        0) {
      return cbc->peers[i];
    }
  }

  return NULL; // Unknown sender
}

static void hold_message(cbcast_t *cbc, cbcast_msg_t *msg,
                         uint16_t sender_pid) {
  cbcast_held_msg_t *held = calloc(1, sizeof(cbcast_held_msg_t));
  // TODO: if ...
  held->message = msg;
  held->sender_pid = sender_pid;
  arrput(cbc->held_buf, held);
}

static void process_held_messages(cbcast_t *cbc) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf);) {
    cbcast_held_msg_t *held_msg = cbc->held_buf[i];
    if (vc_check_causality(cbc->vclock, held_msg->sender_pid,
                           held_msg->message->header->clock) == 0) {
      arrput(cbc->delivery_queue, strdup(held_msg->message->payload));
      vc_inc(cbc->vclock, held_msg->sender_pid);

      // Remove from held buffer
      arrdel(cbc->held_buf, i);
      cbc_msg_free(held_msg->message);
      free(held_msg);
    } else {
      i++; // Only increment if not deleting an element
    }
  }
}
