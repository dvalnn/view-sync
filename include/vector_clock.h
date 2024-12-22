#ifndef _V_CLOCK_H_
#define _V_CLOCK_H_

#include "result.h"
#include <pthread.h>
#include <stdint.h>

struct VectorClock {
  uint64_t *clock;
  uint64_t len;
  pthread_mutex_t mtx;
};

typedef struct VectorClock vector_clock_t;

Result *vc_init(const uint64_t size);
void vc_free(vector_clock_t *vc);
#endif
