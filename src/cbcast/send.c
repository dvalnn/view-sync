#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                cbcast_msg_t *msg);
static Result *cbc_store_sent_message(cbcast_t *cbc, cbcast_msg_t *msg);

void cbc_send(cbcast_t *cbc, cbcast_msg_t *msg) {
  if (!cbc || !msg) {
    fprintf(stderr, "[cbc_send] Invalid arguments\n");
    return;
  }

  // Step 1: Update vector clock and get a snapshot
  msg->header->clock = *(uint64_t *)result_expect(
      vc_inc(cbc->vclock, cbc->pid), "[cbc_send] vclock increment failed");

  // Step 3: Send msg to all peers
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];
    result_expect(cbc_send_to_peer(cbc->socket_fd, peer, msg),
                  "[cbc_send] send failed");
  }

  // Step 4: Store sent message
  result_expect(cbc_store_sent_message(cbc, msg), "[cbc_send] store failed");
}

static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                cbcast_msg_t *msg) {
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  int flags = 0;
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
