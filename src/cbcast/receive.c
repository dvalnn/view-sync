
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cbcast.h"
#include "lib/stb_ds.h"
#include "vector_clock.h"

// Function Prototypes
static int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp);
static char *receive_raw_message(cbcast_t *cbc, struct sockaddr *sender_addr,
                                 socklen_t *addr_len, size_t *recv_len);
static cbcast_peer_t *find_sender(cbcast_t *cbc,
                                  struct sockaddr_in *sender_addr);
static void process_held_messages(cbcast_t *cbc);
static Result *hold_message(cbcast_t *cbc, cbcast_msg_t *msg,
                            uint16_t sender_pid);
static char *process_delivery(cbcast_t *cbc);
// Main Function
char *cbc_rcv(cbcast_t *cbc) {
  if (!cbc)
    return NULL; // Invalid input

  struct sockaddr sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  size_t recv_len = 0;

  // Step 1: Receive the raw message
  char *buffer = receive_raw_message(cbc, &sender_addr, &addr_len, &recv_len);
  if (!buffer) // No data or error
    return process_delivery(cbc);

  // Step 2: Deserialize the received message
  Result *serialize_res = cbc_msg_deserialize(buffer);
  free(buffer);
  if (result_is_err(serialize_res)) {
    result_free(serialize_res);
    return process_delivery(cbc);
  }

  cbcast_msg_t *received = result_unwrap(serialize_res);

  // Step 3: Identify the sender
  struct sockaddr_in sender_ipv4 = *(struct sockaddr_in *)&sender_addr;
  cbcast_peer_t *sender = find_sender(cbc, &sender_ipv4);
  if (!sender) {
    char ipv4[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_ipv4.sin_addr.s_addr, ipv4, INET_ADDRSTRLEN);
    fprintf(stderr, "[cbc_rcv] Unknown sender %s:%d\n", ipv4,
            ntohs(sender_ipv4.sin_port));
    cbc_msg_free(received);
    return process_delivery(cbc);
  }

  // Step 4: Handle the message
  if (vc_check_causality(cbc->vclock, sender->pid, received->header->clock) ==
      0) {
    arrput(cbc->delivery_queue,
           strdup(received->payload)); // Add to delivery queue
    vc_inc(cbc->vclock, sender->pid);  // Update vector clock
    cbc_msg_free(received);
    process_held_messages(cbc); // Process held messages
  } else {
    result_expect(hold_message(cbc, received, sender->pid),
                  "[cbc_rcv] hold_message failed"); // Hold the message
  }

  // Step 5: Return a message from the delivery queue
  return process_delivery(cbc);
}

// ************** Private Functions ***************

// Check causality for the received message
static int vc_check_causality(vector_clock_t *vclock, uint64_t pid,
                              uint64_t timestamp) {
  return (timestamp == vclock->clock[pid] + 1) ? 0 : -1;
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
                                  struct sockaddr_in *sender_addr) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (memcmp(cbc->peers[i]->addr, sender_addr, sizeof(struct sockaddr_in)) ==
        0) {
      return cbc->peers[i];
    }
  }
  return NULL; // Unknown sender
}

// Hold a message that cannot yet be delivered
static Result *hold_message(cbcast_t *cbc, cbcast_msg_t *msg,
                            uint16_t sender_pid) {
  cbcast_held_msg_t *held = calloc(1, sizeof(cbcast_held_msg_t));
  if (!held) {
    return result_new_err("[hold_message] Memory allocation failed");
  }
  held->message = msg;
  held->sender_pid = sender_pid;
  arrput(cbc->held_buf, held);

  return result_new_ok(NULL);
}

// Process and deliver held messages
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
      i++; // Only increment if not removing
    }
  }
}

static char *process_delivery(cbcast_t *cbc) {
  // Return message in delivery queue, if any
  if (arrlen(cbc->delivery_queue) > 0) {
    char *delivered_msg = cbc->delivery_queue[0];
    arrdel(cbc->delivery_queue, 0); // Remove from queue
    return delivered_msg;
  }

  return NULL; // No message ready for delivery
}
