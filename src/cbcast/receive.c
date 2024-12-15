#include "cbcast.h"
#include "lib/stb_ds.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

static int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp);
static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len);
static bool parse_message(const char *buffer, size_t recv_len,
                          uint64_t *timestamp, uint64_t *msg_len,
                          char **message);
static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_addr);
static void hold_message(cbcast_t *cbc, cbcast_peer_t *sender,
                         uint64_t timestamp, const char *message);
static void process_held_messages(cbcast_t *cbc);

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

  // Step 2: Parse the message
  uint64_t timestamp = 0, msg_len = 0;
  char *message = NULL;
  if (!parse_message(buffer, recv_len, &timestamp, &msg_len, &message)) {
    free(buffer);
    return NULL; // Invalid or incomplete message
  }

  // Step 3: Identify the sender
  struct sockaddr_in sender_ipv4 = *(struct sockaddr_in *)&sender_addr;
  cbcast_peer_t *sender = find_sender(cbc, &sender_ipv4);
  if (!sender) {
    fprintf(stderr, "[cbc_rcv] Received message from unknown sender\n");
    free(buffer);
    return NULL;
  }

  // Step 4: Handle the message
  int causal_result = vc_check_causality(cbc->vclock, sender->pid, timestamp);
  if (causal_result == 0) {
    arrput(cbc->delivery_queue, strdup(message)); // Copy to delivery queue
    vc_inc(cbc->vclock, sender->pid);             // Update vector clock

    process_held_messages(cbc);
  } else
    hold_message(cbc, sender, timestamp, message);

  free(buffer);

  // Step 6: Return a message from the delivery queue if available
  if (arrlen(cbc->delivery_queue) > 0) {
    char *delivered_msg = cbc->delivery_queue[0]; // Get the first message
    arrdel(cbc->delivery_queue, 0);               // Remove from delivery queue
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

  return buffer;
}

static bool parse_message(const char *buffer, size_t recv_len,
                          uint64_t *timestamp, uint64_t *msg_len,
                          char **message) {
  if (recv_len < 2 * sizeof(uint64_t)) {
    fprintf(stderr, "[parse_message] Received message is too short\n");
    return false;
  }

  *timestamp = *(uint64_t *)buffer;
  *msg_len = *(uint64_t *)(buffer + sizeof(uint64_t));
  *message = (char *)(buffer + 2 * sizeof(uint64_t));

  if (recv_len < 2 * sizeof(uint64_t) + *msg_len) {
    fprintf(stderr, "[parse_message] Message length mismatch\n");
    return false;
  }

  return true;
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

static void hold_message(cbcast_t *cbc, cbcast_peer_t *sender,
                         uint64_t timestamp, const char *message) {
  cbcast_in_msg_t *held_msg = malloc(sizeof(cbcast_in_msg_t));
  if (!held_msg) {
    fprintf(stderr, "[hold_message] Failed to allocate held message\n");
    return;
  }

  held_msg->pid = sender->pid;
  held_msg->timestamp = timestamp;
  held_msg->payload = strdup(message); // Copy payload

  arrput(cbc->held_buf, held_msg); // Add to held buffer
}

static void process_held_messages(cbcast_t *cbc) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf);) {
    cbcast_in_msg_t *held_msg = cbc->held_buf[i];
    if (vc_check_causality(cbc->vclock, held_msg->pid, held_msg->timestamp) ==
        0) {
      arrput(cbc->delivery_queue, strdup(held_msg->payload));
      vc_inc(cbc->vclock, held_msg->pid);

      // Remove from held buffer
      free(held_msg->payload);
      free(held_msg);
      arrdel(cbc->held_buf, i);
    } else {
      i++; // Only increment if not deleting an element
    }
  }
}
