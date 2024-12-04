#include "view.h"

static int find(uint16_t *arr, uint16_t fd) {
  for (int i = 0; i < arrlen(arr); i++) {
    if (arr[i] == fd) {
      return i;
    }
  }

  return -1;
}

static Result *view_add(view_t *view, uint16_t gmid) {
  // Check if GMID already exists
  if (find(view->gm_ids, gmid) != -1) {
    return result_new_err("GMID already exists");
  }

  arrpush(view->gm_ids, gmid);
  return result_new_ok(view);
}

static Result *view_remove(view_t *view, uint16_t gmid) {
  int idx = find(view->gm_ids, gmid);
  if (idx == -1) {
    return result_new_err("GMID not found");
  }

  arrdelswap(view->gm_ids, idx);
  return result_new_ok(view);
}

// Produces new_view from current_view.
// Destroys current_view.
// Result ->ok has type (view_t *)
Result *next_view(view_t *previous, uint16_t gmid, view_act_t act) {
  view_t *next = malloc(sizeof(view_t));
  if (!next) {
    return result_new_err("Failed to allocate memory for new view");
  }
  *next = *previous;
  next->id++;

  // Invalidate the previous view (optional, depending on use case)
  previous->gm_ids = nullptr;

  switch (act) {
  case V_DROP:
    return view_remove(next, gmid);
  case V_ADD:
    return view_add(next, gmid);
  default:
    return result_new_err("Action undefined");
  }
}
