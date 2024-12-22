#include "cbcast.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <stdlib.h>

// ************** Public Functions ***************
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

  /* printf("[cbc_send] cbc_pid %lu broadcasting message with clock %d\n", */
  /*        cbc->pid, msg->header->clock); */

  pthread_mutex_lock(&cbc->peer_lock);
  for (size_t i = 0; i < (size_t)arrlen(cbc->peers); i++) {
    cbcast_peer_t *peer = cbc->peers[i];
    cbcast_outgoing_msg_t *outgoing =
        result_expect(cbc_outgoing_msg_create(msg, peer->addr),
                      "[cbc_send] Failed to create outgoing message");

    pthread_mutex_lock(&cbc->send_lock);
    arrput(cbc->send_queue, outgoing);
    pthread_mutex_lock(&cbc->send_lock);
  }
  pthread_mutex_unlock(&cbc->peer_lock);

  pthread_cond_signal(&cbc->send_cond);

  return result_new_ok(NULL);
}
