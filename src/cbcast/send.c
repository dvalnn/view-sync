#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

// ************** Function Declaration ***************
//

Result *cbc_store_sent_message(cbcast_t *cbc, cbcast_msg_t *msg);

void cbc_broadcast(cbcast_t *cbc, cbcast_msg_t *msg, int flags);

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

  msg->header->clock = vc_inc(cbc->vclock, cbc->pid);

  printf("[cbc_send] cbc_pid %lu broadcasting message with clock %d\n",
         cbc->pid, msg->header->clock);

  cbc_broadcast(cbc, msg, 0);

  return cbc_store_sent_message(cbc, msg);
}

// ************** Private Functions ***************
//

void cbc_broadcast(cbcast_t *cbc, cbcast_msg_t *msg, int flags) {
  size_t msg_len = 0;
  char *msg_bytes = cbc_msg_serialize(msg, &msg_len);

  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    result_expect(
        cbc_send_to_peer(cbc, cbc->peers[i], msg_bytes, msg_len, flags),
        "[cbc_send_broadcast] send failed");
  }

  free(msg_bytes);
}

// Stores the sent message in sent_msgs and returns a Result
Result *cbc_store_sent_message(cbcast_t *cbc, cbcast_msg_t *msg) {

  cbcast_sent_msg_t *out = malloc(sizeof(cbcast_sent_msg_t));
  if (!out) {
    return result_new_err("[cbc_store_sent_message] malloc out");
  }
  out->confirms = calloc(arrlen(cbc->peers), sizeof(*out->confirms));
  out->message = msg;

  arrput(cbc->sent_buf, out);
  return result_new_ok(NULL);
}
