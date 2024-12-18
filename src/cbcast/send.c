#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

// ************** Function Declaration ***************
//
static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                cbcast_msg_t *msg, int flags);

static Result *cbc_store_sent_message(cbcast_t *cbc, cbcast_msg_t *msg);

static void cbc_broadcast(cbcast_t *cbc, cbcast_msg_t *msg, int flags);

// ************** Public Functions ***************
//
void cbc_send(cbcast_t *cbc, cbcast_msg_t *msg) {
  if (!cbc || !msg) {
    fprintf(stderr, "[cbc_send] Invalid arguments\n");
    return;
  }

  switch (msg->header->kind) {
  case CBC_HEARTBEAT:
    cbc_broadcast(cbc, msg, 0);
    return;

  case CBC_DATA:
    msg->header->clock = *(uint64_t *)result_expect(
        vc_inc(cbc->vclock, cbc->pid), "[cbc_send] vclock increment failed");
    printf("[cbc_send] Broadcasting message with clock %d\n",
           msg->header->clock);
    cbc_broadcast(cbc, msg, 0);
    result_expect(cbc_store_sent_message(cbc, msg), "[cbc_send] store failed");
    break;

  case CBC_RETRANSMIT:
    // TODO: Implement retransmit
    RESULT_UNIMPLEMENTED;
  }
}

// ************** Private Functions ***************
//
static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                cbcast_msg_t *msg, int flags) {
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  size_t payload_size = 0;
  const char *payload = cbc_msg_serialize(msg, &payload_size);
  ssize_t sent_bytes =
      sendto(socket_fd, payload, payload_size, flags,
             (struct sockaddr *)peer->addr, sizeof(struct sockaddr_in));

  if (sent_bytes < 0) {
    return result_new_err("[cbc_send_to_peer] sendto failed");
  }

  return result_new_ok(NULL);
}

static void cbc_broadcast(cbcast_t *cbc, cbcast_msg_t *msg, int flags) {
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];
    result_expect(cbc_send_to_peer(cbc->socket_fd, peer, msg, flags),
                  "[cbc_send_heartbeat] send failed");
  }
}

// Stores the sent message in sent_msgs and returns a Result
static Result *cbc_store_sent_message(cbcast_t *cbc, cbcast_msg_t *msg) {

  cbcast_sent_msg_t *out = malloc(sizeof(cbcast_sent_msg_t));
  if (!out) {
    return result_new_err("[cbc_store_sent_message] malloc out");
  }
  out->confirms = calloc(arrlen(cbc->peers), sizeof(*out->confirms));
  out->message = msg;

  arrput(cbc->sent_buf, out);
  return result_new_ok(NULL);
}
