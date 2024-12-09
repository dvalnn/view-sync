#include "view.h"

static int find(uint16_t *arr, uint16_t fd) {
  for (int i = 0; i < arrlen(arr); i++) {
    if (arr[i] == fd) {
      return i;
    }
  }

  return -1;
}

int compare_uint16(const void *a, const void *b) {
  uint16_t val_a = *(uint16_t *)a;
  uint16_t val_b = *(uint16_t *)b;

  return (val_a > val_b) -
         (val_a < val_b); // Return positive, zero, or negative
}

static Result *view_add(view_t *view, uint16_t gmid) {
  // Check if GMID already exists
  if (find(view->gm_ids, gmid) != -1) {
    return result_new_err("GMID already exists");
  }

  arrpush(view->gm_ids, gmid);
  return result_new_ok("");
}

static Result *view_remove(view_t *view, uint16_t gmid) {
  int idx = find(view->gm_ids, gmid);
  if (idx == -1) {
    return result_new_err("GMID not found");
  }

  arrdelswap(view->gm_ids, idx);
  return result_new_ok("");
}

Result *exec_view_change(view_t *view, const view_change_t change) {
  switch (change.action) {
  case V_DROP:
    return view_remove(view, change.gm_id);

  case V_ADD:
    return view_add(view, change.gm_id);

  default:
    return result_new_err("Action undefined");
  }
}

// Produces next_view from previous
// Destroys (frees) previous
// Result ->ok has type (view_t *)
Result *next_view(view_t *previous, view_change_t *changes, size_t n_changes) {
  if (!previous || !changes || n_changes <= 0) {
    return result_new_err("next_view() called with invalid arguments");
  }

  view_t *next_view = (view_t *)malloc(sizeof(view_t));
  if (!next_view) {
    return result_new_err("Failed to allocate memory for new view");
  }

  next_view->id = ++previous->id;
  next_view->gm_ids = NULL; // Ensure independent array management

  // Copy previous gm_ids to next_view
  for (size_t i = 0; i < (size_t)arrlen(previous->gm_ids); i++) {
    arrput(next_view->gm_ids, previous->gm_ids[i]);
  }

  // Apply changes
  for (size_t i = 0; i < n_changes; i++) {
    Result *res = exec_view_change(next_view, changes[i]);
    if (result_is_err(res)) {
      arrfree(next_view->gm_ids); // Free dynamic array
      free(next_view);            // Free view struct
      return res;
    }
  }

  // Sort gm_ids after applying changes
  qsort(next_view->gm_ids, arrlen(next_view->gm_ids), sizeof(uint16_t),
        (int (*)(const void *, const void *))compare_uint16);

  view_free(previous);
  return result_new_ok(next_view);
}

Result *init_view(uint16_t *gm_ids, size_t n_ids) {
  view_t *new_view = (view_t *)malloc(sizeof(view_t));
  if (!new_view) {
    return result_new_err("Failed to allocate memory for new view");
  };

  // init pointer
  new_view->id = 0;
  new_view->gm_ids = NULL;

  for (size_t i = 0; i < n_ids; i++) {
    arrput(new_view->gm_ids, gm_ids[i]);
  }

  // Verify the populated gm_ids
  if ((size_t)arrlen(new_view->gm_ids) != n_ids) {
    free(new_view); // Clean up allocated memory
    return result_new_err("Failed to populate gm_ids");
  }

  return result_new_ok(new_view);
}

void view_free(view_t *v) {
  if (!v) {
    // Force crash with error message
    // efectivelly an assert
    result_unwrap(result_new_err("trying to free null view"));
  }

  arrfree(v->gm_ids);
  free(v);
}
