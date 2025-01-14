#include "cbcast.h"
#include "lib/stb_ds.h"

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
#include "unistd.h"
#endif
#endif

#include <stdio.h>
#include <stdlib.h>

void handle_old_messages(cbcast_t *cbc);
uint16_t get_current_clock(cbcast_t *cbc);
cbcast_sent_msg_t **find_old_messages(cbcast_t *cbc, uint16_t current_clock);
uint64_t *find_dead_peers(cbcast_t *cbc, cbcast_sent_msg_t **dead_msgs);
void handle_dead_peers(cbcast_t *cbc, uint64_t *dead_peers);
void retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t **old_msgs);
uint64_t *find_unacked_peers(cbcast_t *cbc, cbcast_sent_msg_t *old_sent);
void queue_retransmit_messages(cbcast_t *cbc, cbcast_sent_msg_t *old_sent,
                               uint64_t *target_peers);
cbcast_sent_msg_t **filter_dead_messages(uint64_t current_clock,
                                         cbcast_sent_msg_t **old_msgs);

Result *cbc_send(cbcast_t *cbc, const char *payload, const size_t payload_len) {
  if (!cbc || !payload || !payload_len) {
    return result_new_err("[cbc_send] Invalid arguments");
  }

  Result *msg_create_res = NULL;
  switch (cbc->state) {
  case CBC_STATE_NORMAL:
  case CBC_STATE_PEER_SUSPECTED:
    msg_create_res = cbc_msg_create(CBC_DATA, payload, payload_len);
    break;

  case CBC_STATE_DISCONNECTED:
    return result_new_err("[cbc_send] Cannot send in disconnected state");
  }

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

  handle_old_messages(cbc);

  pthread_cond_signal(&cbc->send_cond);

  return result_new_ok(NULL);
}

int arrfind(uint64_t *arr, uint64_t fd) {
  for (int i = 0; i < arrlen(arr); i++) {
    if (arr[i] == fd) {
      return i;
    }
  }

  return -1;
}

uint64_t *find_dead_peers(cbcast_t *cbc, cbcast_sent_msg_t **dead_msgs) {
  uint64_t *dead_peers = NULL;
  for (size_t i = 0; i < (size_t)arrlen(dead_msgs); i++) {
    cbcast_sent_msg_t *old_sent = dead_msgs[i];
    uint64_t *target_peers = find_unacked_peers(cbc, old_sent);

    if (!target_peers || arrlen(target_peers) == 0) {
      return RESULT_UNREACHABLE;
    }

    for (size_t j = 0; j < (size_t)arrlen(target_peers); j++) {
      if (arrfind(dead_peers, target_peers[j]) == -1) {
        arrput(dead_peers, target_peers[j]);
      }
    }
  }

  return dead_peers;
}

void handle_dead_peers(cbcast_t *cbc, uint64_t *dead_peers) {
  if (!cbc || !dead_peers) {
    return (void)RESULT_UNREACHABLE;
  }

  if (arrlen(dead_peers) == 0) {
    return;
  }

  pthread_mutex_lock(&cbc->peer_lock);
  if (arrlen(cbc->peers) == 0) {
    return (void)RESULT_UNREACHABLE;
  }

  int total_peers = arrlen(cbc->peers);
  int remaining_peers = arrlen(cbc->peers) - arrlen(dead_peers);
  pthread_mutex_unlock(&cbc->peer_lock);

  // If no peers remain, terminate the broadcast context
  if (remaining_peers < 1) {
    printf("[handle_dead_peers] cbc pid %lu no peers remain. Disconnected\n",
           cbc->pid);
    cbc->state = CBC_STATE_DISCONNECTED;
  }

  // If remaining peers are in the minority, terminate the broadcast context
  // and notify the user do not have enough peers to continue
  if (remaining_peers < total_peers / 2) {
    printf("[handle_dead_peers] cbc pid %lu minority of peers remain. "
           "Disconnected\n",
           cbc->pid);

    cbc->state = CBC_STATE_DISCONNECTED;
  }

  // enter peer transition mode -- no messages are exchanged untill there is
  // agreement on the new set of peers
  // 1. Flag dead peers with PEER_SUSPECT state.
  // 2. Flag the need for view change protocol.

  // stop message exchange. Halt sent and receive threads
  pthread_mutex_lock(&cbc->send_lock);
  pthread_mutex_lock(&cbc->recv_lock);
  pthread_mutex_lock(&cbc->peer_lock);

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (arrfind(dead_peers, cbc->peers[i]->pid) != -1) {
      cbc->peers[i]->state = CBC_PEER_DEAD;
    }
  }
  arrfree(dead_peers);

  cbc->state = CBC_STATE_PEER_SUSPECTED;

  pthread_mutex_unlock(&cbc->peer_lock);
  pthread_mutex_unlock(&cbc->recv_lock);
  pthread_mutex_unlock(&cbc->send_lock);
}

#define CBC_OLD_MSG_THRESHOLD 2
#define CBC_DEAD_MSG_THRESHOLD 8
void handle_old_messages(cbcast_t *cbc) {
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

  cbcast_sent_msg_t **dead_msgs = filter_dead_messages(current_clock, old_msgs);
  if (cbc->state == CBC_STATE_NORMAL) {
    uint64_t *dead_peers = find_dead_peers(cbc, dead_msgs);

    if (arrlen(dead_peers) > 0) {
      handle_dead_peers(cbc, dead_peers);
      arrfree(dead_peers);
    }
  }
  arrfree(dead_msgs);

  retransmit_messages(cbc, old_msgs);
}

uint16_t get_current_clock(cbcast_t *cbc) {
  pthread_mutex_lock(&cbc->vclock->mtx);
  uint16_t current_clock = cbc->vclock->clock[cbc->pid];
  pthread_mutex_unlock(&cbc->vclock->mtx);
  return current_clock;
}

cbcast_sent_msg_t **filter_dead_messages(uint64_t current_clock,
                                         cbcast_sent_msg_t **old_msgs) {
  cbcast_sent_msg_t **dead_msgs = NULL;

  for (size_t i = 0; i < (size_t)arrlen(old_msgs); i++) {
    cbcast_sent_msg_t *old_sent = old_msgs[i];
    uint16_t msg_age = current_clock - old_sent->message->header->clock;

    if (msg_age >= CBC_DEAD_MSG_THRESHOLD) {
      arrput(dead_msgs, old_sent);
      arrdel(old_msgs, i);
    }
  }

  return dead_msgs;
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

    printf("[retransmit_old_with_missing_acks] cbc pid %lu old message clock "
           "%d ack state %lu\n",
           cbc->pid, old_sent->message->header->clock, old_sent->ack_bitmap);

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
      continue;
    }
    arrput(target_peers, cbc->peers[j]->pid);
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

    printf("[retransmit_old_with_missing_acks] cbc pid %lu retransmitting "
           "message clock %d to peer %lu\n",
           cbc->pid, outgoing->message->header->clock, cbc->peers[j]->pid);

    outgoing->socket_flags = 0;
    outgoing->message->header->clock = old_sent->message->header->clock;

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

#ifdef STATISTICS
  uint64_t n_dropped = 0;
  uint64_t n_sent = 0;
#endif

  pthread_mutex_lock(&cbc->peer_lock);
  {
    for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
      if (cbc->peers[i]->state == CBC_PEER_DEAD) {
        continue;
      }

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
#ifdef STATISTICS
        n_dropped++;
#endif
        continue;
      }
#endif
#endif

      struct sockaddr_in *addr = cbc->peers[i]->addr;
      sendto(cbc->socket_fd, msg_bytes, msg_size, flags,
             (struct sockaddr *)addr, sizeof(*addr));
#ifdef STATISTICS
      n_sent++;
#endif
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);

#ifdef STATISTICS
  pthread_mutex_lock(&cbc->stats_lock);
  cbc->stats->sent_msg_count += n_sent;
  cbc->stats->dropped_msg_count += n_dropped;
  pthread_mutex_unlock(&cbc->stats_lock);
#endif

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

#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP
    bool will_drop = false;

    if (rand() % 100 < NETWORK_SIMULATION_DROP) {
      printf("[cbc_send_thread] cbc pid %lu dropping message type %d\n",
             cbc->pid, outgoing->message->header->kind);

      will_drop = true;
    }
#endif
#endif

    switch (outgoing->message->header->kind) {
      // View change is handled the same as data, but has priority
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
#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP

      if (will_drop) {
#ifdef STATISTICS
        pthread_mutex_lock(&cbc->stats_lock);
        cbc->stats->dropped_ack_count++;
        pthread_mutex_unlock(&cbc->stats_lock);
#endif
        break;
      }

#ifdef STATISTICS
      pthread_mutex_lock(&cbc->stats_lock);
      cbc->stats->sent_ack_count++;
      pthread_mutex_unlock(&cbc->stats_lock);
#endif

      goto send;
#endif
#endif

    case CBC_RETRANSMIT_REQ:
#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP

      if (will_drop) {
#ifdef STATISTICS
        pthread_mutex_lock(&cbc->stats_lock);
        cbc->stats->dropped_retransmit_req_count++;
        pthread_mutex_unlock(&cbc->stats_lock);
#endif
        break;
      }

#ifdef STATISTICS
      pthread_mutex_lock(&cbc->stats_lock);
      cbc->stats->sent_retransmit_req_count++;
      pthread_mutex_unlock(&cbc->stats_lock);
#endif

      goto send;
#endif
#endif

    case CBC_RETRANSMIT:
#ifdef NETWORK_SIMULATION
#ifdef NETWORK_SIMULATION_DROP

      if (will_drop) {
#ifdef STATISTICS
        pthread_mutex_lock(&cbc->stats_lock);
        cbc->stats->dropped_ack_count++;
        pthread_mutex_unlock(&cbc->stats_lock);
#endif
        break;
      }

#ifdef STATISTICS
      pthread_mutex_lock(&cbc->stats_lock);
      cbc->stats->sent_ack_count++;
      pthread_mutex_unlock(&cbc->stats_lock);
#endif

    send:
#endif
#endif

      sendto(cbc->socket_fd, msg_bytes, msg_size, outgoing->socket_flags,
             (struct sockaddr *)outgoing->addr, sizeof(*outgoing->addr));
      break;
    }

    free(msg_bytes);
    cbc_outgoing_msg_free(outgoing);
  }

  return NULL;
}
