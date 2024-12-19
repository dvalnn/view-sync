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

  Result *vc_inc_res = vc_inc(cbc->vclock, cbc->pid);
  if (result_is_err(vc_inc_res)) {
    cbc_msg_free(msg);
    return vc_inc_res;
  }
  msg->header->clock =
      *(uint64_t *)result_expect(vc_inc_res, "unfallible expect");

  printf("[cbc_send] cbc_pid %lu broadcasting message with clock %d\n",
         cbc->pid, msg->header->clock);

  cbc_broadcast(cbc, msg, 0);

  return cbc_store_sent_message(cbc, msg);
}

Result *cbc_send_to_peer(const cbcast_t *cbc, const cbcast_peer_t *peer,
                         const char *payload, const size_t payload_len,
                         const int flags) {
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
    result_expect(
        cbc_send_to_peer(cbc, cbc->peers[i], msg_bytes, msg_len, flags),
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
