#include "vector_clock.h"
#include "lib/stb_ds.h"
#include <stdlib.h>

Result *vc_init(const uint64_t size) {
  if (!size) {
    return result_new_err("[vc_init] invalid size");
  }

  vector_clock_t *vc = malloc(sizeof(vector_clock_t));
  if (!vc) {
    return result_new_err("[vc_init] failed to allocate vector_clock_t");
  }

  vc->clock = malloc(sizeof(uint64_t) * size);
  if (!vc->clock) {
    free(vc);
    return result_new_err("[vc_init] failed to allocate vc->clock");
  }
  vc->len = size;
  memset(vc->clock, 0, vc->len * sizeof(*vc->clock));

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

uint64_t vc_inc(vector_clock_t *vc, uint64_t pos) {
  pthread_mutex_lock(&vc->mtx);
  uint64_t incd = ++vc->clock[pos];
  pthread_mutex_unlock(&vc->mtx);

  return incd;
}

Result *vc_snapshot(vector_clock_t *vc) {
  if (!vc) {
    return result_new_err("[vc_snapshot] vc is null");
  }

  pthread_mutex_lock(&vc->mtx);

  uint64_t *snap_vec = malloc(sizeof(uint64_t) * vc->len);
  memcpy(snap_vec, vc->clock, sizeof(uint64_t) * vc->len);

  pthread_mutex_unlock(&vc->mtx);

  return result_new_ok(snap_vec);
}
