#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

// ************** Function Declaration ***************
//

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
    printf("[cbc_send] Worker %lu broadcasting message with clock %d\n",
           cbc->pid, msg->header->clock);
    cbc_broadcast(cbc, msg, 0);
    result_expect(cbc_store_sent_message(cbc, msg), "[cbc_send] store failed");
    break;

  case CBC_RETRANSMIT:
    // TODO: Implement retransmit
    return (void)RESULT_UNIMPLEMENTED;

    // ACKs are directly sent by the receive function
  case CBC_ACK:
    return (void)RESULT_UNREACHABLE;
  }
}

Result *cbc_send_to_peer(const cbcast_t *cbc, const char *payload,
                         const size_t payload_len, const int peer_idx,
                         const int flags) {
  cbcast_peer_t *peer = cbc->peers[peer_idx];
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  ssize_t sent_bytes =
      sendto(cbc->socket_fd, payload, payload_len, flags,
             (struct sockaddr *)peer->addr, sizeof(struct sockaddr_in));

  if (sent_bytes < 0) {
    return result_new_err("[cbc_send_to_peer] sendto failed");
  }

  return result_new_ok(NULL);
}

// ************** Private Functions ***************
//

static void cbc_broadcast(cbcast_t *cbc, cbcast_msg_t *msg, int flags) {
  size_t msg_len = 0;
  char *msg_bytes = cbc_msg_serialize(msg, &msg_len);

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    result_expect(cbc_send_to_peer(cbc, msg_bytes, msg_len, i, flags),
                  "[cbc_send_heartbeat] send failed");
  }

  free(msg_bytes);
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
