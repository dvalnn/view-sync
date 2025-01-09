#include "cbcast.h"
#include "lib/stb_ds.h"

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
#include "unistd.h"
#endif
#endif

#include <stdio.h>
#include <stdlib.h>

void retransmit_old_with_missing_acks(cbcast_t *cbc);
uint16_t get_current_clock(cbcast_t *cbc);
cbcast_sent_msg_t **find_old_messages(cbcast_t *cbc, uint16_t current_clock);
void retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t **old_msgs);
uint64_t *find_unacked_peers(cbcast_t *cbc, cbcast_sent_msg_t *old_sent);
void queue_retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t *old_sent,
                               uint64_t *target_peers);

Result *cbc_send(cbcast_t *cbc, const char *payload, const size_t payload_len) {
  if (!cbc || !payload || !payload_len) {
    return result_new_err("[cbc_send] Invalid arguments");
  }

  if (arrlen(cbc->peers) == 0) {
    return result_new_err("[cbc_send] No peers to send to");
  }

  Result *msg_create_res = cbc_msg_create(CBC_DATA, payload, payload_len);
  if (result_is_err(msg_create_res)) {
    return msg_create_res;
  }
  cbcast_msg_t *msg = result_expect(msg_create_res, "unfallible expect");

  pthread_mutex_lock(&cbc->vclock->mtx);
  msg->header->clock = ++cbc->vclock->clock[cbc->pid];
  pthread_mutex_unlock(&cbc->vclock->mtx);

  cbcast_outgoing_msg_t *outgoing =
      result_expect(cbc_outgoing_msg_create(msg, NULL),
                    "[cbc_send] Failed to create outgoing message");

  printf("[cbc_send] Sending message: \"%s\" %d\n", payload,
         msg->header->clock);

  pthread_mutex_lock(&cbc->send_lock);
  arrput(cbc->send_queue, outgoing);
  pthread_mutex_unlock(&cbc->send_lock);

  retransmit_old_with_missing_acks(cbc);

  pthread_cond_signal(&cbc->send_cond);

  return result_new_ok(NULL);
}

#define CBC_OLD_MSG_THRESHOLD 2
void retransmit_old_with_missing_acks(cbcast_t *cbc) {
  if (!cbc) {
    return (void)RESULT_UNREACHABLE;
  }

  if (arrlen(cbc->sent_msg_buffer) == 0 || arrlen(cbc->peers) == 0) {
    return;
  }

  uint16_t current_clock = get_current_clock(cbc);

  cbcast_sent_msg_t **old_msgs = find_old_messages(cbc, current_clock);
  if (!old_msgs || arrlen(old_msgs) == 0) {
    return;
  }

  retransmit_messages(cbc, old_msgs);
}

uint16_t get_current_clock(cbcast_t *cbc) {
  pthread_mutex_lock(&cbc->vclock->mtx);
  uint16_t current_clock = cbc->vclock->clock[cbc->pid];
  pthread_mutex_unlock(&cbc->vclock->mtx);
  return current_clock;
}

cbcast_sent_msg_t **find_old_messages(cbcast_t *cbc, uint16_t current_clock) {
  cbcast_sent_msg_t **old_msgs = NULL;

  pthread_mutex_lock(&cbc->send_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i];
    uint16_t msg_age = current_clock - sent_msg->message->header->clock;

    if (msg_age >= CBC_OLD_MSG_THRESHOLD &&
        sent_msg->ack_bitmap != sent_msg->ack_target) {
      arrput(old_msgs, sent_msg);
    }
  }
  pthread_mutex_unlock(&cbc->send_lock);

  return old_msgs;
}

void retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t **old_msgs) {
  while (arrlen(old_msgs) > 0) {
    cbcast_sent_msg_t *old_sent = arrpop(old_msgs);
    if (!old_sent) {
      continue;
    }

    uint64_t *target_peers = find_unacked_peers(cbc, old_sent);

    if (!target_peers || arrlen(target_peers) == 0) {
      return (void)RESULT_UNREACHABLE;
    }

    queue_retransmit_messages(cbc, old_sent, target_peers);
  }

  arrfree(old_msgs);
}

uint64_t *find_unacked_peers(cbcast_t *cbc, cbcast_sent_msg_t *old_sent) {
  uint64_t *target_peers = NULL;

  pthread_mutex_lock(&cbc->peer_lock);
  for (size_t j = 0; j < (size_t)arrlen(cbc->peers); j++) {
    if (old_sent->ack_bitmap & (1 << cbc->peers[j]->pid)) {
      arrput(target_peers, cbc->peers[j]->pid);
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);

  return target_peers;
}

void queue_retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t *old_sent,
                               uint64_t *target_peers) {
  for (size_t j = 0; j < (size_t)arrlen(target_peers); j++) {
    struct sockaddr_in *addr = cbc_peer_get_addr_copy(cbc, target_peers[j]);

    cbcast_msg_t *dup_msg =
        result_expect(cbc_msg_create(CBC_RETRANSMIT, old_sent->message->payload,
                                     old_sent->message->header->len),
                      "[retransmit_old_with_missing_acks] Failed to create "
                      "retransmit message - out of memory");

    cbcast_outgoing_msg_t *outgoing =
        result_expect(cbc_outgoing_msg_create(dup_msg, addr),
                      "[retransmit_old_with_missing_acks] Failed to create "
                      "outgoing message - out of memory");

    printf("[retransmit_old_with_missing_acks] Retransmitting message: %d to "
           "peer %lu\n",
           outgoing->message->header->clock, cbc->peers[j]->pid);

    outgoing->socket_flags = 0;

    pthread_mutex_lock(&cbc->send_lock);
    arrput(cbc->send_queue, outgoing);
    pthread_mutex_unlock(&cbc->send_lock);
  }

  arrfree(target_peers);
}

// @brief Broadcasts a message to all peers in the network.
// @param cbc The broadcast context to use for sending the message.
// @param msg_bytes The serialized message to broadcast.
// @param msg_size The size of the serialized message.
// @param flags The flags to use for the sendto call.
// @return A bitfield representing the peers that should acknowledge the
// message.
uint64_t broadcast(cbcast_t *cbc, const char *msg_bytes, const size_t msg_size,
                   int flags) {

  uint64_t ack_target = 0;
  pthread_mutex_lock(&cbc->peer_lock);
  {
    for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
      ack_target |= 1 << cbc->peers[i]->pid;

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
      if (rand() % 100 < NETWORK_SIMULATION_DROP) {

        cbcast_msg_kind_t kind = *(cbcast_msg_kind_t *)(msg_bytes);
        uint16_t msg_clock =
            *(uint16_t *)(msg_bytes + sizeof(cbcast_msg_kind_t));

        printf("[broadcast] cbc pid %lu dropping message type %d clock %hu to "
               "peer %lu\n",
               cbc->pid, kind, msg_clock, cbc->peers[i]->pid);

        continue;
      }
#endif
#endif

      struct sockaddr_in *addr = cbc->peers[i]->addr;
      sendto(cbc->socket_fd, msg_bytes, msg_size, flags,
             (struct sockaddr *)addr, sizeof(*addr));
    }
  }

  pthread_mutex_unlock(&cbc->peer_lock);
  return ack_target;
}

void *cbc_send_thread(void *arg) {
  cbcast_t *cbc = (cbcast_t *)arg;

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
  // Seed the random number generator
  int seed = time(NULL) ^ getpid();
  srand(seed); // Combine time and process ID for unique seed
#endif
#endif

  for (;;) {
    pthread_mutex_lock(&cbc->send_lock);
    while (arrlen(cbc->send_queue) == 0) {
      pthread_cond_wait(&cbc->send_cond, &cbc->send_lock);
    }

    cbcast_outgoing_msg_t *outgoing = cbc->send_queue[0];
    arrdel(cbc->send_queue, 0);
    pthread_mutex_unlock(&cbc->send_lock);

    size_t msg_size = 0;
    char *msg_bytes = cbc_msg_serialize(outgoing->message, &msg_size);

    switch (outgoing->message->header->kind) {
    case CBC_DATA:
      uint64_t ack_target = broadcast(cbc, msg_bytes, msg_size, 0);
      cbcast_sent_msg_t *sent =
          result_expect(cbc_sent_msg_create(outgoing->message, ack_target),
                        "conversion between outgoing message and sent message "
                        "should not fail");

      pthread_mutex_lock(&cbc->send_lock);
      arrput(cbc->sent_msg_buffer, sent);
      pthread_mutex_unlock(&cbc->send_lock);

      outgoing->message = NULL; // Prevent double free
      break;

    case CBC_ACK:
    case CBC_RETRANSMIT_REQ:
    case CBC_RETRANSMIT:

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
      if (rand() % 100 < NETWORK_SIMULATION_DROP) {
        printf("[cbc_send_thread] cbc pid %lu dropping message type %d\n",
               cbc->pid, outgoing->message->header->kind);
        break;
      }
#endif
#endif

      sendto(cbc->socket_fd, msg_bytes, msg_size, outgoing->socket_flags,
             (struct sockaddr *)outgoing->addr, sizeof(*outgoing->addr));
      break;

    case CBC_HEARTBEAT:
      return RESULT_UNIMPLEMENTED;
      break;
    }

    free(msg_bytes);
    cbc_outgoing_msg_free(outgoing);
  }

  return NULL;
}
