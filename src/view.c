#include "view.h"
#include "view_sync.h"

#include "pthread.h"
#include "unistd.h"
#include <stdio.h>
#include <time.h>

static int arrfind(uint64_t *arr, uint64_t fd) {
  for (int i = 0; i < arrlen(arr); i++) {
    if (arr[i] == fd) {
      return i;
    }
  }

  return -1;
}

static int view_compare_uint64(const void *a, const void *b) {
  uint64_t val_a = *(uint64_t *)a;
  uint64_t val_b = *(uint64_t *)b;

  return (val_a > val_b) -
         (val_a < val_b); // Return positive, zero, or negative
}

static Result *view_add(view_t *view, uint64_t gmid) {
  // Check if GMID already exists
  if (arrfind(view->gm_ids, gmid) != -1) {
    return result_new_err("GMID already exists");
  }

  arrpush(view->gm_ids, gmid);
  return result_new_ok("");
}

static Result *view_remove(view_t *view, uint64_t gmid) {
  int idx = arrfind(view->gm_ids, gmid);
  if (idx == -1) {
    return result_new_err("GMID not found");
  }

  arrdelswap(view->gm_ids, idx);
  return result_new_ok("");
}
