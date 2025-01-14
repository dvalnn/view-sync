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

char *serialize_view_sync_msg(view_sync_msg_t *msg, size_t *out_size) {
  if (!msg || !out_size) {
    return NULL;
  }

  size_t total_size =
      sizeof(view_sync_msg_type_t) + sizeof(uint32_t) + msg->payload_len;
  char *serialized = calloc(total_size, sizeof(char));
  if (!serialized) {
    return NULL;
  }

  memcpy(serialized, msg, sizeof(view_sync_msg_type_t));
  memcpy(serialized + sizeof(view_sync_msg_type_t), &msg->payload_len,
         sizeof(msg->payload_len));
  memcpy(serialized + sizeof(view_sync_msg_type_t) + sizeof(msg->payload_len),
         msg->payload, msg->payload_len);

  *out_size = total_size;
  return serialized;
}

view_sync_msg_t *deserialize_view_sync_msg(const char *bytes) {
  if (!bytes) {
    return NULL;
  }

  view_sync_msg_t *msg = calloc(1, sizeof(view_sync_msg_t));
  if (!msg) {
    return NULL;
  }

  memcpy(msg, bytes, sizeof(view_sync_msg_t));
  msg->payload = calloc(msg->payload_len, sizeof(char));
  if (!msg->payload) {
    free(msg);
    return NULL;
  }

  memcpy(msg->payload, bytes + sizeof(view_sync_msg_t), msg->payload_len);
  return msg;
}

void view_change_coordinate(cbcast_t *cbc) {
  // 0. Filter peers that are dead
  pthread_mutex_lock(&cbc->peer_lock);
  uint16_t *dead_peers = NULL;
  uint16_t *peers_for_ack = NULL;
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    switch (cbc->peers[i]->state) {

    case CBC_PEER_ALIVE:
      arrput(peers_for_ack, cbc->peers[i]->pid);
      break;

    case CBC_PEER_DEAD:
      arrput(dead_peers, cbc->peers[i]->pid);
      break;
    }
  }
  pthread_mutex_unlock(&cbc->peer_lock);

  // 1. Message every live peer about the dead peers
  size_t serialized_size = 0;
  char *serialized_message = serialize_view_sync_msg(
      &(view_sync_msg_t){
          .type = VIEW_SYNC_CHANGE_REQ, .payload_len = 0, .payload = NULL},
      &serialized_size);

  result_unwrap(cbc_send(cbc, serialized_message, serialized_size));
  free(serialized_message);

  // 2. Wait for all live peers to acknowledge the dead peers
  while (arrlen(peers_for_ack) > 0) {
    usleep(1000); // Sleep for 1ms
    cbcast_received_msg_t *msg = cbc_receive(cbc);
    if (!msg) {
      continue;
    }

    char *payload = msg->message->payload;
    view_sync_msg_t *view_sync_msg = deserialize_view_sync_msg(payload);
    if (view_sync_msg->type == VIEW_SYNC_ACK) {
      for (size_t i = 0; i < (size_t)arrlen(peers_for_ack); i++) {
        if (peers_for_ack[i] == msg->sender_pid) {
          arrdel(peers_for_ack, i);
        }
        break;
      }
    }
    free(view_sync_msg->payload);
    free(view_sync_msg);

    cbc_received_msg_free(msg);
  }

  // 3. If all live peers have acknowledged the dead peers send commit message
  // Commit message payload is the id of coordinator and the number of dead
  char *commit_msg = serialize_view_sync_msg(
      &(view_sync_msg_t){.type = VIEW_SYNC_COMMIT,
                         .payload_len =
                             (uint32_t)arrlen(dead_peers) * sizeof(uint32_t),
                         .payload = (char *)dead_peers},
      &serialized_size);
  result_unwrap(cbc_send(cbc, commit_msg, sizeof(view_sync_msg_t)));

  // 5. Update ack targets for sent messages, remove dead peers
  pthread_mutex_lock(&cbc->send_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i];
    for (size_t j = 0; j < (size_t)arrlen(dead_peers); j++) {
      // Mark the dead peers as acked. Sent buffer cleanup
      sent_msg->ack_bitmap |= 1 << dead_peers[j];
      if (sent_msg->ack_bitmap == sent_msg->ack_target) {
        arrdel(cbc->sent_msg_buffer, i);
        cbc_sent_msg_free(sent_msg);
      }
    }
  }
  pthread_mutex_unlock(&cbc->send_lock);

  cbc->state = CBC_STATE_NORMAL;

  arrfree(dead_peers);
  arrfree(peers_for_ack);
}

void view_change_wait_commit(cbcast_t *cbc, uint16_t coordinator_pid) {
  // 1. Wait for the coordinator to send the commit message
  bool commit_received = false;
  char *payload = NULL;
  cbcast_received_msg_t *received = NULL;
  while (!commit_received) {
    received = cbc_receive(cbc);
    if (!received) {
      continue;
    }

    if (received->sender_pid == coordinator_pid) {
      char *payload = received->message->payload;
      view_sync_msg_t *view_sync_msg = deserialize_view_sync_msg(payload);
      if (view_sync_msg->type == VIEW_SYNC_COMMIT) {
        commit_received = true;
        free(view_sync_msg);
      }
      payload = view_sync_msg->payload;
      break;
    }

    cbc_received_msg_free(received);
  }

  uint32_t *n_dead_peers = (uint32_t *)payload;
  uint16_t *dead_peers = (uint16_t *)(payload + sizeof(uint32_t));

  // 2. Update ack targets for sent messages, remove dead peers
  pthread_mutex_lock(&cbc->send_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->sent_msg_buffer); i++) {
    cbcast_sent_msg_t *sent_msg = cbc->sent_msg_buffer[i];
    for (size_t j = 0; j < (size_t)n_dead_peers; j++) {
      // Mark the dead peers as acked. Sent buffer cleanup
      sent_msg->ack_bitmap |= 1 << dead_peers[j];
      if (sent_msg->ack_bitmap == sent_msg->ack_target) {
        arrdel(cbc->sent_msg_buffer, i);
        cbc_sent_msg_free(sent_msg);
      }
    }
  }
  pthread_mutex_unlock(&cbc->send_lock);

  free(payload);
  cbc_received_msg_free(received);

  cbc->state = CBC_STATE_NORMAL;
}

void view_change_follow(cbcast_t *cbc) {
  bool change_req_received = false;
  view_sync_msg_t *view_sync_msg = NULL;
  uint16_t coordinator_pid = 0;

  while (!change_req_received) {
    cbcast_received_msg_t *msg = cbc_receive(cbc);
    if (!msg) {
      continue;
    }

    char *payload = msg->message->payload;
    view_sync_msg = deserialize_view_sync_msg(payload);
    if (view_sync_msg->type == VIEW_SYNC_CHANGE_REQ) {
      change_req_received = true;
      coordinator_pid = msg->sender_pid;
    }

    free(view_sync_msg->payload);
    free(view_sync_msg);

    cbc_received_msg_free(msg);
  }

  view_change_wait_commit(cbc, coordinator_pid);
}

Result *view_sync_cbc_send(cbcast_t *cbc, const char *payload,
                           const size_t payload_len) {

  if (cbc->state == CBC_STATE_DISCONNECTED) {
    return result_new_err("[cbc_send] Cannot send in disconnected state");
  }

  if (cbc->state == CBC_STATE_PEER_SUSPECTED) {
    // Check if self is the coordinator or not and transition into the view
    // change protocol accordingly

    // Coordinator is the cbc with lowest pid among live peers and self
    bool is_coordinator = true;
    pthread_mutex_lock(&cbc->peer_lock);
    for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
      if (cbc->peers[i]->pid < cbc->pid &&
          cbc->peers[i]->state == CBC_PEER_ALIVE) {
        is_coordinator = false;
        break;
      }
    }
    pthread_mutex_unlock(&cbc->peer_lock);

    if (is_coordinator) {
      view_change_coordinate(cbc);
    } else {
      view_change_follow(cbc);
    }
  }

  if (cbc->state == CBC_STATE_NORMAL) {
    // print msg in hex
    /* printf("Payload: "); */
    /* for (size_t i = 0; i < payload_len; i++) { */
    /*   printf("%02x", payload[i]); */
    /* } */
    /* printf("\n"); */

    char *wrapped_payload = NULL;
    size_t wrapped_payload_len = 0;
    view_sync_msg_t msg = (view_sync_msg_t){.type = VIEW_SYNC_DATA,
                                            .payload_len = payload_len,
                                            .payload = (char *)payload};
    wrapped_payload = serialize_view_sync_msg(&msg, &wrapped_payload_len);
    // print msg in hex
    /* printf("Wrapped Payload: "); */
    /* for (size_t i = 0; i < wrapped_payload_len; i++) { */
    /*   printf("%02x", wrapped_payload[i]); */
    /* } */
    /* printf("\n"); */

    return cbc_send(cbc, wrapped_payload, wrapped_payload_len);
  }

  return RESULT_UNREACHABLE;
}

cbcast_received_msg_t *view_sync_cbc_receive(cbcast_t *cbc) {
  if (cbc->state == CBC_STATE_DISCONNECTED) {
    return NULL;
  }

  if (cbc->state == CBC_STATE_PEER_SUSPECTED) {
    bool is_coordinator = true;
    pthread_mutex_lock(&cbc->peer_lock);
    for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
      if (cbc->peers[i]->pid < cbc->pid &&
          cbc->peers[i]->state == CBC_PEER_ALIVE) {
        is_coordinator = false;
        break;
      }
    }
    pthread_mutex_unlock(&cbc->peer_lock);

    if (is_coordinator) {
      view_change_coordinate(cbc);
    } else {
      view_change_follow(cbc);
    }
  }

  cbcast_received_msg_t *msg = cbc_receive(cbc);
  // unwrap the message and check if it is a view sync message
  if (!msg) {
    return NULL;
  }

  view_sync_msg_t *view_sync_msg =
      deserialize_view_sync_msg(msg->message->payload);

  switch (view_sync_msg->type) {
  case VIEW_SYNC_DATA:
    free(msg->message->payload);
    msg->message->payload = view_sync_msg->payload;
    msg->message->header->len = view_sync_msg->payload_len;
    return msg;

  case VIEW_SYNC_CHANGE_REQ:
    view_change_wait_commit(cbc, msg->sender_pid);
    return NULL;

  case VIEW_SYNC_ACK:
  case VIEW_SYNC_COMMIT:
    return RESULT_UNREACHABLE;
  }

  return RESULT_UNREACHABLE;
}
