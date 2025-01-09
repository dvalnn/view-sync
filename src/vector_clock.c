#include "vector_clock.h"
#include "lib/stb_ds.h"
#include <stdlib.h>

// TODO: refactor vector clock using dynamic array to be in line with the rest
// of the codebase
Result *vc_init(const uint64_t size) {
  if (!size) {
    return result_new_err("[vc_init] invalid size");
  }

  vector_clock_t *vc = calloc(1, sizeof(vector_clock_t));
  if (!vc) {
    return result_new_err("[vc_init] failed to allocate vector_clock_t");
  }

  vc->clock = calloc(size, sizeof(uint64_t));
  if (!vc->clock) {
    free(vc);
    return result_new_err("[vc_init] failed to allocate vc->clock");
  }
  vc->len = size;

  pthread_mutexattr_t mtx_attrs;
  pthread_mutexattr_init(&mtx_attrs);
  if (pthread_mutex_init(&vc->mtx, &mtx_attrs) != 0) {
    free(vc);
    return result_new_err("[vc_init] Failed to initialize vector clock mutex");
  }

  return result_new_ok(vc);
}

void vc_free(vector_clock_t *vc) {
  if (!vc) {
    return;
  }

  pthread_mutex_destroy(&vc->mtx);
  free(vc->clock);
  free(vc);
}

uint64_t *vc_snapshot(vector_clock_t *vc) {
  if (!vc) {
    return NULL;
  }

  pthread_mutex_lock(&vc->mtx);
  uint64_t *snapshot = calloc(vc->len, sizeof(uint64_t));
  if (!snapshot) {
    pthread_mutex_unlock(&vc->mtx);
    return NULL;
  }

  memcpy(snapshot, vc->clock, vc->len * sizeof(uint64_t));
  pthread_mutex_unlock(&vc->mtx);

  return snapshot;
}
