#include "cbcast.h"
#include "lib/stb_ds.h"

#include <stdlib.h>

Result *cbc_init(uint64_t pid, uint64_t max_p) {
  if (pid < 0 || max_p <= 0 || pid >= max_p) {
    return result_new_err("[cbc_init] invalid pid");
  }

  cbcast_t *new = malloc(sizeof(cbcast_t));
  if (new == NULL) {
    return result_new_err("[cbc_init] cbcast malloc failed");
  }

  pthread_mutexattr_t mtx_attrs;
  pthread_mutexattr_init(&mtx_attrs);
  pthread_mutex_init(&new->vclock.clock_mtx, &mtx_attrs);

  new->self_pid = pid;
  new->vclock.clock = NULL;
  for (uint64_t i = 0; i < max_p; i++) {
    arrput(new->vclock.clock, 0);
  }
  if ((uint64_t)arrlen(new->vclock.clock) != pid) {
    return result_new_err("[cbc_init] clock init failed");
  }

  new->peers = NULL;
  new->held_msgs = NULL;
  new->ready_msgs = NULL;
  new->sent_msgs = NULL;

  return result_new_ok(new);
}

void cbc_free(cbcast_t *cbc) {
  pthread_mutex_destroy(&cbc->vclock.clock_mtx);

  // TODO: free array elements before freeing array
  arrfree(cbc->peers);
  arrfree(cbc->held_msgs);
  arrfree(cbc->ready_msgs);
  arrfree(cbc->sent_msgs);

  free(cbc);
}

void cbc_send(cbcast_t *cbc, char *msg) {
  // TODO: Construct msg_hdr struct

  pthread_mutex_lock(&cbc->vclock.clock_mtx);
  // Maybe create local snapshot of vclock to reduce lock time
  cbc->vclock.clock[cbc->self_pid] += 1;
  // TODO: send msg to every peer
  //
  (void)msg;

  pthread_mutex_unlock(&cbc->vclock.clock_mtx);

  // TODO: add msg to sent msgs
}

char *cbc_rcv(cbcast_t *cbc);
