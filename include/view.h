#ifndef _VIEW_H_
#define _VIEW_H_

#include <stdint.h>

#include "lib/stb_ds.h"

#include "result.h"

struct View {
  uint16_t id;
  uint16_t *gm_ids;
};
typedef struct View view_t;

enum ViewAct {
  V_DROP,
  V_ADD,
};
typedef enum ViewAct view_act_t;

struct ViewChange {
  uint16_t gm_id;
  view_act_t action;
};
typedef struct ViewChange view_change_t;

/**
 * @brief Initializes a new view with the specified GMIDs.
 *
 * Allocates and initializes a new `view_t` structure, populating it with the
 * provided GMIDs. The GMIDs are stored in a dynamically allocated array.
 *
 * @param gm_ids A pointer to an array of GMIDs to populate the view.
 * @param n_ids The number of GMIDs in the `gm_ids` array.
 * @return A `Result` containing a pointer to the new view if successful,
 *         or an error message if initialization fails.
 */
Result *view_init(uint16_t *gm_ids, size_t n_ids);

/**
 * @brief Frees the memory associated with a view.
 *
 * Releases the memory allocated for the GMIDs array and the view structure.
 * If a null pointer is passed, the program will crash with an error message.
 *
 * @param v A pointer to the `view_t` structure to free.
 */
void view_free(view_t *v);

/**
 * @brief Produces a new view based on a previous view and a series of changes.
 *
 * Applies a set of changes to a given view to produce the next view. The
 * function frees the previous view and returns the newly created view.
 * The GMIDs array is sorted after applying the changes.
 *
 * @param previous A pointer to the previous `view_t` structure.
 * @param changes An array of `view_change_t` structures describing the changes
 * to apply.
 * @param n_changes The number of changes in the `changes` array.
 * @return A `Result` containing a pointer to the new view if successful,
 *         or an error message if an error occurs during the operation.
 */
Result *view_next(view_t *previous, view_change_t *changes, size_t n_changes);

#endif
