#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                       uint64_t timestamp) {
  uint64_t current = vclock->clock[pid];
  return (timestamp == current + 1) ? 0 : -1; // 0 = deliverable, -1 = hold
}

char *cbc_rcv(cbcast_t *cbc) {
  if (!cbc) {
    return NULL; // Invalid input
  }

  char *buffer = NULL;
  size_t recv_len = 0;

  // Step 1: Allocate a buffer for receiving data
  size_t buffer_size = 1024; // Adjust size based on expected message sizes
  buffer = malloc(buffer_size);
  if (!buffer) {
    fprintf(stderr, "[cbc_rcv] Failed to allocate receive buffer\n");
    return NULL;
  }

  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);

  // Step 2: Receive raw data
  recv_len = recvfrom(cbc->socket_fd, buffer, buffer_size, 0,
                      (struct sockaddr *)&sender_addr, &addr_len);
  if (recv_len <= 0) {
    free(buffer);
    return NULL; // No data or error in receiving
  }

  // Step 3: Parse the message
  if (recv_len < 2 * sizeof(uint64_t)) {
    fprintf(stderr, "[cbc_rcv] Received message is too short\n");
    free(buffer);
    return NULL;
  }

  uint64_t timestamp = *(uint64_t *)buffer;     // First 8 bytes
  uint64_t msg_len = *(uint64_t *)(buffer + 8); // Next 8 bytes
  char *message = buffer + 16;                  // Remaining bytes

  if (recv_len < 16 + msg_len) {
    fprintf(stderr, "[cbc_rcv] Message length mismatch\n");
    free(buffer);
    return NULL;
  }

  // Identify the sender's PID
  cbcast_peer_t *sender = NULL;
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, &sender_addr, sizeof(struct sockaddr_in)) ==
        0) {
      sender = cbc->peers[i];
      break;
    }
  }

  if (!sender) {
    fprintf(stderr, "[cbc_rcv] Received message from unknown sender\n");
    free(buffer);
    return NULL;
  }

  // Step 4: Handle the message based on causality
  int causal_result = vc_check_causality(cbc->vclock, sender->pid, timestamp);
  if (causal_result == 0) {
    // Causality satisfied; deliver the message
    arrput(cbc->delivery_queue, strdup(message)); // Copy to delivery queue
    vc_inc(cbc->vclock, sender->pid);             // Update vector clock

    // Check held messages to see if any become deliverable
    for (size_t i = 0; i < (size_t)arrlen(cbc->held_buf);) {
      cbcast_in_msg_t *held_msg = cbc->held_buf[i];
      if (held_msg->pid == sender->pid &&
          vc_check_causality(cbc->vclock, held_msg->pid, held_msg->timestamp) ==
              0) {
        // Deliverable message
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
  } else {
    // Causality not satisfied; hold the message
    cbcast_in_msg_t *held_msg = malloc(sizeof(cbcast_in_msg_t));
    if (!held_msg) {
      fprintf(stderr, "[cbc_rcv] Failed to allocate held message\n");
      free(buffer);
      return NULL;
    }

    held_msg->pid = sender->pid;
    held_msg->timestamp = timestamp;
    held_msg->payload = strdup(message); // Copy payload

    arrput(cbc->held_buf, held_msg); // Add to held buffer
  }

  // Free the original buffer
  free(buffer);

  // Step 5: Return a message from the delivery queue if available
  if (arrlen(cbc->delivery_queue) > 0) {
    char *delivered_msg = cbc->delivery_queue[0]; // Get the first message
    arrdel(cbc->delivery_queue, 0);               // Remove from delivery queue
    return delivered_msg;
  }

  return NULL; // No message ready for delivery
}
