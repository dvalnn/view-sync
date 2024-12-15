#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

static Result *cbc_create_payload(uint64_t ts, char *msg, size_t msg_len);
static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                char *payload, size_t payload_len);
static Result *cbc_store_sent_message(cbcast_t *cbc, char *payload,
                                      size_t payload_len);

void cbc_send(cbcast_t *cbc, char *msg, size_t msg_len) {
  if (!cbc || !msg || msg_len == 0) {
    fprintf(stderr, "[cbc_send] Invalid arguments\n");
    return;
  }

  // Step 1: Update vector clock and get a snapshot
  uint64_t ts = *(uint64_t *)result_expect(
      vc_inc(cbc->vclock, cbc->pid), "[cbc_send] vclock increment failed");

  // Step 2: Create payload
  size_t payload_len = msg_len + 2 * sizeof(uint64_t);
  char *payload = result_expect(cbc_create_payload(ts, msg, msg_len),
                                "[cbc_send] payload create failed");

  // Step 3: Send payload to all peers
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];
    result_expect(cbc_send_to_peer(cbc->socket_fd, peer, payload, payload_len),
                  "[cbc_send] send failed");
  }

  // Step 4: Store sent message
  result_expect(cbc_store_sent_message(cbc, payload, payload_len),
                "[cbc_send] store failed");

  // Cleanup
  free(payload);
}

// ************************************************
// ************************************************
// ************** Private Functions ***************
// ************************************************
// ************************************************

// Creates the payload and returns it as a Result
static Result *cbc_create_payload(uint64_t ts, char *msg, size_t msg_len) {
  size_t payload_len = 2 * sizeof(uint64_t) + msg_len;
  char *payload = malloc(payload_len);
  if (!payload) {
    return result_new_err(
        "[cbc_create_payload] Failed to allocate memory for payload");
  }

  // Copy vector clock and message into the payload
  memcpy(payload, &ts, sizeof(ts));
  memcpy(payload + sizeof(ts), &msg_len, sizeof(msg_len));
  memcpy(payload + sizeof(ts) + sizeof(msg_len), msg, msg_len);

  return result_new_ok(payload);
}

static Result *cbc_send_to_peer(int socket_fd, cbcast_peer_t *peer,
                                char *payload, size_t payload_len) {
  if (!peer || !peer->addr) {
    return result_new_err("[cbc_send_to_peer] Invalid peer or address");
  }

  int flags = 0;
  ssize_t sent_bytes =
      sendto(socket_fd, payload, payload_len, flags,
             (struct sockaddr *)peer->addr, sizeof(struct sockaddr_in));
  if (sent_bytes < 0) {
    return result_new_err("[cbc_send_to_peer] sendto failed");
  }

  return result_new_ok(NULL);
}

// Stores the sent message in sent_msgs and returns a Result
static Result *cbc_store_sent_message(cbcast_t *cbc, char *payload,
                                      size_t payload_len) {

  cbcast_out_msg_t *out = malloc(sizeof(cbcast_out_msg_t));
  if (!out) {
    return result_new_err("[cbc_store_sent_message] malloc out");
  }
  out->confirms = malloc(sizeof(char) * arrlen(cbc->peers));
  memset(out->confirms, 0, arrlen(cbc->peers));

  out->payload = malloc(sizeof(char) * payload_len);
  memcpy(out->payload, payload, sizeof(char) * payload_len);

  out->payload_len = payload_len;
  arrput(cbc->sent_buf, out);

  return result_new_ok(NULL);
}
