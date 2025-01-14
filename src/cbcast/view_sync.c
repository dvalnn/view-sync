#include "cbcast.h"
#include "lib/stb_ds.h"
#include "unistd.h"
#include <stdio.h>

enum ViewSyncMessageType {
  VIEW_SYNC_DATA = 1,
  VIEW_SYNC_CHANGE_REQ,
  VIEW_SYNC_ACK,
  VIEW_SYNC_COMMIT,
};
typedef enum ViewSyncMessageType view_sync_msg_type_t;

struct view_sync_msg {
  view_sync_msg_type_t type;
  uint32_t payload_len;
  char *payload;
};
typedef struct view_sync_msg view_sync_msg_t;

// Utility functions
char *allocate_and_copy(const void *src, size_t size) {
  char *dest = calloc(size, sizeof(char));
  if (dest && src) {
    memcpy(dest, src, size);
  }
  return dest;
}

// Serialize view sync message
char *serialize_view_sync_msg(view_sync_msg_t *msg, size_t *out_size) {
  if (!msg || !out_size)
    return NULL;

  size_t total_size =
      sizeof(view_sync_msg_type_t) + sizeof(uint32_t) + msg->payload_len;
  char *serialized = allocate_and_copy(NULL, total_size);
  if (!serialized)
    return NULL;

  memcpy(serialized, &msg->type, sizeof(view_sync_msg_type_t));
  memcpy(serialized + sizeof(view_sync_msg_type_t), &msg->payload_len,
         sizeof(uint32_t));
  if (msg->payload_len > 0 && msg->payload) {
    memcpy(serialized + sizeof(view_sync_msg_type_t) + sizeof(uint32_t),
           msg->payload, msg->payload_len);
  }

  *out_size = total_size;
  return serialized;
}

// Deserialize view sync message
view_sync_msg_t *deserialize_view_sync_msg(const char *wrapped_bytes,
                                           size_t wrapped_bytes_len) {
  if (!wrapped_bytes ||
      wrapped_bytes_len < sizeof(view_sync_msg_type_t) + sizeof(uint32_t))
    return NULL;

  view_sync_msg_t *msg = calloc(1, sizeof(view_sync_msg_t));
  if (!msg)
    return NULL;

  memcpy(&msg->type, wrapped_bytes, sizeof(view_sync_msg_type_t));
  memcpy(&msg->payload_len, wrapped_bytes + sizeof(view_sync_msg_type_t),
         sizeof(uint32_t));

  // wrapped_bytes_len should be == payload_len + sizeof(view_sync_msg_type_t) +
  // sizeof(uint32_t) + 1 (null terminator)
  if (wrapped_bytes_len !=
      msg->payload_len + sizeof(view_sync_msg_type_t) + sizeof(uint32_t)) {

    printf("wrapped_bytes_len: %lu, unwrapped_bytes_len: %u\n",
           wrapped_bytes_len, msg->payload_len);

    return RESULT_UNREACHABLE;
  }

  if (msg->payload_len > 0) {
    msg->payload = allocate_and_copy(
        wrapped_bytes + sizeof(view_sync_msg_type_t) + sizeof(uint32_t),
        msg->payload_len + 1); // +1 for null terminator

    if (!msg->payload) {
      free(msg);
      return NULL;
    }
  }

  return msg;
}

// Helper to free view_sync_msg_t
void free_view_sync_msg(view_sync_msg_t *msg) {
  if (msg) {
    free(msg->payload);
    free(msg);
  }
}

// Filter peers by state
void filter_peers(cbcast_t *cbc, uint16_t **alive, uint16_t **dead) {
  pthread_mutex_lock(&cbc->peer_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    uint16_t pid = cbc->peers[i]->pid;
    switch (cbc->peers[i]->state) {
    case CBC_PEER_ALIVE:
      arrput(*alive, pid);
      break;
    case CBC_PEER_DEAD:
      arrput(*dead, pid);
      break;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);
}

// Notify peers of dead nodes
void notify_peers_of_dead(cbcast_t *cbc, uint16_t *dead_peers,
                          size_t dead_count) {
  size_t serialized_size = 0;
  char *msg = serialize_view_sync_msg(
      &(view_sync_msg_t){.type = VIEW_SYNC_CHANGE_REQ,
                         .payload_len = dead_count * sizeof(uint16_t),
                         .payload = (char *)dead_peers},
      &serialized_size);
  result_unwrap(cbc_send(cbc, msg, serialized_size));
  free(msg);
}

// Notify peers of dead nodes
void ack_change_req(cbcast_t *cbc) {
  size_t serialized_size = 0;
  char *p = "AAAHHH";
  char *msg = serialize_view_sync_msg(
      &(view_sync_msg_t){
          .type = VIEW_SYNC_ACK, .payload_len = strlen(p), .payload = p},
      &serialized_size);
  result_unwrap(cbc_send(cbc, msg, serialized_size));
  free(msg);
}

// Handle peer acknowledgments
void wait_for_peer_ack(cbcast_t *cbc, uint16_t **peers_for_ack) {
  int sleep_count = 0;
  while (arrlen(*peers_for_ack) > 0) {
    printf("[view_wait_for_peer_ack] cbc pid %lu sleeping\n", cbc->pid);
    sleep(1); // 5ms sleep

    if (sleep_count % 2 == 0) {
      printf("[view_wait_for_peer_ack] cbc pid %lu flushing\n", cbc->pid);
      cbc_flush(cbc);
    }

    cbcast_received_msg_t *msg = cbc_receive(cbc);
    if (!msg) {
      printf("[view_wait_for_peer_ack] cbc pid %lu no message\n", cbc->pid);
      printf("[view_wait_for_peer_ack] cbc pid %lu queue sizes: send %lu, sent "
             "%lu, held %lu, delivery %lu\n",
             cbc->pid, arrlen(cbc->send_queue), arrlen(cbc->sent_msg_buffer),
             arrlen(cbc->held_msg_buffer), arrlen(cbc->delivery_queue));
      continue;
    }

    view_sync_msg_t *view_msg = deserialize_view_sync_msg(
        msg->message->payload, msg->message->header->len);
    // TODO: Handle the case where the message is not a view sync message
    //       Store the message in a separate buffer and reprocess it once the
    //       view change protocol is complete
    printf("[view_wait_for_peer_ack] cbc pid %lu received message type %d\n",
           cbc->pid, view_msg->type);

    if (view_msg->type == VIEW_SYNC_ACK) {
      for (size_t i = 0; i < (size_t)arrlen(*peers_for_ack); i++) {
        if ((*peers_for_ack)[i] == msg->sender_pid) {
          arrdel(*peers_for_ack, i);
          printf("[view_wait_for_peer_ack] cbc pid %lu peer %d acked\n",
                 cbc->pid, msg->sender_pid);
          break;
        }
      }
    }
    free_view_sync_msg(view_msg);
    cbc_received_msg_free(msg);
  }
}

void update_sent_buffer(cbcast_t *cbc, uint16_t *dead_peers,
                        size_t dead_count) {
  (void)cbc;
  (void)dead_peers;
  (void)dead_count;
  return;

  /* pthread_mutex_lock(&cbc->send_lock); */
  /* size_t *fully_acked_indices = NULL; */
  /* for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) { */
  /*   cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i]; */
  /*   for (size_t j = 0; j < dead_count; j++) { */
  /*     sent_msg->ack_bitmap |= 1 << dead_peers[j]; */
  /*   } */
  /*   // Check if all acks have been received for the message */
  /*   if (sent_msg->ack_bitmap == sent_msg->ack_target) { */
  /*     // Store the index of the fully acked message */
  /*     arrput(fully_acked_indices, i); */
  /*   } */
  /* } */
  /**/
  /* // Remove fully acked messages from the sent buffer */
  /* for (size_t i = 0; i < (size_t)arrlen(fully_acked_indices); i++) { */
  /*   arrdel(cbc->sent_msg_buffer, fully_acked_indices[i]); */
  /* } */
  /* pthread_mutex_unlock(&cbc->send_lock); */
  /**/
  /* arrfree(fully_acked_indices); */
}

void view_change_coordinate(cbcast_t *cbc) {

  printf("[view_change_coordinate] cbc pid %lu coordinating view change\n",
         cbc->pid);

  // 0. Filter peers that are dead
  uint16_t *dead_peers = NULL;
  uint16_t *peers_for_ack = NULL;
  filter_peers(cbc, &peers_for_ack, &dead_peers);

  printf("[view_change_coordinate] cbc pid %lu dead peers: ", cbc->pid);
  for (size_t i = 0; i < (size_t)arrlen(dead_peers); i++) {
    printf("%d ", dead_peers[i]);
  }
  printf("\n");

  // 1. Message every live peer about the dead peers
  printf("[view_change_coordinate] cbc pid %lu notifying peers of dead\n",
         cbc->pid);
  notify_peers_of_dead(cbc, dead_peers, arrlen(dead_peers));

  printf("[view_change_coordinate] cbc pid %lu waiting for peer ack\n",
         cbc->pid);

  // 2. Wait for all live peers to acknowledge the dead peers
  wait_for_peer_ack(cbc, &peers_for_ack);

  printf("[view_change_coordinate] cbc pid %lu all peers acked\n", cbc->pid);
  // 3. If all live peers have acknowledged the dead peers send commit message
  // Commit message payload is the id of coordinator and the number of dead
  size_t serialized_size = 0;
  char *commit_msg = serialize_view_sync_msg(
      &(view_sync_msg_t){.type = VIEW_SYNC_COMMIT,
                         .payload_len = arrlen(dead_peers) * sizeof(uint16_t),
                         .payload = (char *)dead_peers},
      &serialized_size);
  result_unwrap(cbc_send(cbc, commit_msg, sizeof(view_sync_msg_t)));
  free(commit_msg);

  printf("[view_change_coordinate] cbc pid %lu sent commit\n", cbc->pid);

  // 5. Update ack targets for sent messages, remove dead peers
  update_sent_buffer(cbc, dead_peers, arrlen(dead_peers));
  cbc->state = CBC_STATE_NORMAL;

  printf("[view_change_coordinate] cbc pid %lu updated sent buffer\n",
         cbc->pid);

  arrfree(dead_peers);
  arrfree(peers_for_ack);
}

void mark_peers_dead(cbcast_t *cbc, uint16_t *dead_peers, size_t dead_count) {
  pthread_mutex_lock(&cbc->peer_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    for (size_t j = 0; j < dead_count; j++) {
      if (cbc->peers[i]->pid == dead_peers[j]) {
        cbc->peers[i]->state = CBC_PEER_DEAD;
        break;
      }
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);
}

// Wait for commit during view change
void view_change_wait_commit(cbcast_t *cbc, uint16_t coordinator_pid) {

  // Ack view change
  ack_change_req(cbc);

  cbcast_received_msg_t *received = NULL;
  int sleep_count = 0;
  printf("[view_change_wait_commit] cbc pid %lu waiting for commit\n",
         cbc->pid);
  while (true) {
    sleep(1); // 5ms sleep

    if (sleep_count % 2 == 0) {
      cbc_flush(cbc);
    }

    received = cbc_receive(cbc);
    if (!received || received->sender_pid != coordinator_pid) {
      cbc_received_msg_free(received);
      continue;
    }

    view_sync_msg_t *view_msg = deserialize_view_sync_msg(
        received->message->payload, received->message->header->len);
    if (view_msg->type == VIEW_SYNC_COMMIT) {
      printf("[view_change_wait_commit] cbc pid %lu received commit\n",
             cbc->pid);
      uint32_t *n_dead_peers = (uint32_t *)view_msg->payload;
      uint16_t *dead_peers = (uint16_t *)(view_msg->payload + sizeof(uint32_t));

      mark_peers_dead(cbc, dead_peers, *n_dead_peers);
      update_sent_buffer(cbc, dead_peers, *n_dead_peers);

      free_view_sync_msg(view_msg);
      cbc_received_msg_free(received);
      break;
    }

    free_view_sync_msg(view_msg);
    cbc_received_msg_free(received);
  }

  cbc->state = CBC_STATE_NORMAL;
}

// Helper function to determine if the current node is the coordinator
bool is_coordinator(cbcast_t *cbc) {
  bool coordinator = true;
  pthread_mutex_lock(&cbc->peer_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    if (cbc->peers[i]->pid < cbc->pid &&
        cbc->peers[i]->state == CBC_PEER_ALIVE) {
      coordinator = false;
      break;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);
  return coordinator;
}

// Simplify view_change_follow
void view_change_follow(cbcast_t *cbc) {
  printf("[view_change_follow] cbc pid %lu following view change\n", cbc->pid);

  bool change_req_received = false;
  uint16_t coordinator_pid = 0;

  while (!change_req_received) {
    sleep(1000);

    cbcast_received_msg_t *msg = cbc_receive(cbc);
    if (!msg) {
      continue;
    }

    view_sync_msg_t *view_sync_msg = deserialize_view_sync_msg(
        msg->message->payload, msg->message->header->len);
    printf("[view_change_follow] view_sync_msg->type: %d\n",
           view_sync_msg->type);

    switch (view_sync_msg->type) {
    case VIEW_SYNC_DATA:
      printf("[view_change_follow] cbc pid %lu received data clock %d\n",
             cbc->pid, msg->message->header->clock);
      break;

    case VIEW_SYNC_CHANGE_REQ:
      printf("[view_change_follow] cbc pid %lu received change req from %d "
             "clock %d\n",
             cbc->pid, coordinator_pid, msg->message->header->clock);

      change_req_received = true;
      coordinator_pid = msg->sender_pid;
      break;
    case VIEW_SYNC_ACK:
      printf("[view_change_follow] cbc pid %lu received ack clock %d\n",
             cbc->pid, msg->message->header->clock);
      break;
    case VIEW_SYNC_COMMIT:
      printf("[view_change_follow] cbc pid %lu received commit clock %d\n",
             cbc->pid, msg->message->header->clock);
      break;
    }

    free(view_sync_msg);
    cbc_received_msg_free(msg);
  }

  printf("[view_change_follow] cbc pid %lu received change req from %d\n",
         cbc->pid, coordinator_pid);

  view_change_wait_commit(cbc, coordinator_pid);
}

// Refactor view_sync_cbc_send
Result *view_sync_cbc_send(cbcast_t *cbc, const char *payload,
                           const size_t payload_len) {
  if (cbc->state == CBC_STATE_DISCONNECTED) {
    return result_new_err("[cbc_send] Cannot send in disconnected state");
  }

  if (cbc->state == CBC_STATE_PEER_SUSPECTED) {
    printf("[view_sync_cbc_send] cbc pid %lu state peer suspected\n", cbc->pid);
    if (is_coordinator(cbc)) {
      view_change_coordinate(cbc);
    } else {
      view_change_follow(cbc);
    }
  }

  if (cbc->state == CBC_STATE_NORMAL) {
    view_sync_msg_t msg = {.type = VIEW_SYNC_DATA,
                           .payload_len = payload_len,
                           .payload = (char *)payload};

    size_t wrapped_payload_len = 0;
    char *wrapped_payload = serialize_view_sync_msg(&msg, &wrapped_payload_len);
    Result *send_res = cbc_send(cbc, wrapped_payload, wrapped_payload_len);
    free(wrapped_payload);
    return send_res;
  }

  return RESULT_UNREACHABLE;
}

// Refactor view_sync_cbc_receive
cbcast_received_msg_t *view_sync_cbc_receive(cbcast_t *cbc) {
  if (cbc->state == CBC_STATE_DISCONNECTED) {
    return NULL;
  }

  if (cbc->state == CBC_STATE_PEER_SUSPECTED) {
    printf("[view_sync_cbc_receive] cbc pid %lu state peer suspected\n",
           cbc->pid);
    if (is_coordinator(cbc)) {
      view_change_coordinate(cbc);
    } else {
      view_change_follow(cbc);
    }
  }

  cbcast_received_msg_t *msg = cbc_receive(cbc);
  if (!msg) {
    return NULL;
  }

  view_sync_msg_t *view_sync_msg = deserialize_view_sync_msg(
      msg->message->payload, msg->message->header->len);

  switch (view_sync_msg->type) {
  case VIEW_SYNC_DATA:
    printf("[view_sync_cbc_receive] cbc pid %lu received data clock %d\n",
           cbc->pid, msg->message->header->clock);
    free(msg->message->payload);
    msg->message->payload = view_sync_msg->payload;
    msg->message->header->len = view_sync_msg->payload_len;
    free(view_sync_msg);
    return msg;

  case VIEW_SYNC_CHANGE_REQ:
    printf("[view_sync_cbc_receive] cbc pid %lu received change req clock %d\n",
           cbc->pid, msg->message->header->clock);
    printf("[view_sync_cbc_receive] cbc pid %lu waiting for commit\n",
           cbc->pid);
    view_change_wait_commit(cbc, msg->sender_pid);
    free(view_sync_msg);
    cbc_received_msg_free(msg);
    return NULL;

  default:
    free(view_sync_msg);
    cbc_received_msg_free(msg);
    return NULL;
  }
}
