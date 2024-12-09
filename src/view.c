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
Result *next_view(view_t *previous, view_change_t *changes) {
  view_t *next = malloc(sizeof(view_t));
  if (!next) {
    return result_new_err("Failed to allocate memory for new view");
  }
  *next = *previous;
  next->id++;

  // Invalidate the previous view (optional, depending on use case)
  previous->gm_ids = nullptr;

  for (int i = 0; arrlen(changes); i++) {
    switch (changes[i].action) {
    case V_DROP:
      return view_remove(next, changes->gm_id);

    case V_ADD:
      return view_add(next, changes->gm_id);

    default:
      return result_new_err("Action undefined");
    }
  }
  return result_new_err("next_view() called without changes");
}
