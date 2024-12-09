#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include "view.h" // Replace with the actual header file for the library
#include <cmocka.h>

// Test case 1: Add a new GMID
static void test_next_view_add_gmid(void **state) {
  (void)state;

  uint16_t initial_gm_ids[] = {10};
  Result *result = init_view(initial_gm_ids, 1);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *initial_view = (view_t *)result_unwrap(result);
  assert_non_null(initial_view);
  assert_int_equal(initial_view->id, 0);
  assert_int_equal(initial_view->gm_ids[0], 10);

  view_change_t changes1[] = {{7, V_ADD}};
  result = next_view(initial_view, changes1, 1);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *updated_view = (view_t *)result_unwrap(result);
  assert_non_null(updated_view);
  assert_int_equal(updated_view->id, 1);
  // Array should be sorted
  assert_int_equal(updated_view->gm_ids[0], 7);
  assert_int_equal(updated_view->gm_ids[1], 10);

  view_free(updated_view);
}

// Test case 2: Remove an existing GMID
static void test_next_view_remove_gmid(void **state) {
  (void)state;

  uint16_t initial_gm_ids[] = {10, 7};
  Result *result = init_view(initial_gm_ids, 2);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *initial_view = result_unwrap(result);
  assert_non_null(initial_view);

  view_change_t changes2[] = {{7, V_DROP}};
  result = next_view(initial_view, changes2, 1);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *updated_view = result_unwrap(result);
  assert_non_null(updated_view);
  assert_int_equal(updated_view->id, 1);
  assert_int_equal(updated_view->gm_ids[0], 10);
  assert_int_equal(arrlen(updated_view->gm_ids), 1);

  view_free(updated_view);
}

// Test case 3: Try to remove a non-existent GMID
static void test_next_view_remove_nonexistent_gmid(void **state) {
  (void)state;

  uint16_t initial_gm_ids[] = {10};
  Result *result = init_view(initial_gm_ids, 1);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *initial_view = result_unwrap(result);
  assert_non_null(initial_view);

  view_change_t changes3[] = {{42, V_DROP}};
  result = next_view(initial_view, changes3, 1);
  assert_non_null(result);
  assert_true(result_is_err(result));

  result_free(result);
}

static void test_next_view_multiple_changes(void **state) {
  (void)state;

  // Initial GMIDs
  uint16_t initial_gm_ids[] = {10, 20, 30};
  Result *result = init_view(initial_gm_ids, 3);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *previous = result_unwrap(result);
  assert_non_null(previous);
  assert_int_equal(previous->id, 0);
  assert_int_equal(arrlen(previous->gm_ids), 3);
  assert_int_equal(previous->gm_ids[0], 10);
  assert_int_equal(previous->gm_ids[1], 20);
  assert_int_equal(previous->gm_ids[2], 30);

  // Multiple changes: Add and Drop GMIDs
  view_change_t changes[] = {
      {40, V_ADD},  // Add 40
      {50, V_ADD},  // Add 50
      {20, V_DROP}, // Remove 20
      {30, V_DROP}  // Remove 30
  };

  result = next_view(previous, changes, 4);
  assert_non_null(result);
  assert_false(result_is_err(result));

  view_t *updated_view = result_unwrap(result);
  assert_non_null(updated_view);
  assert_int_equal(updated_view->id, 1);
  // Remaining GMIDs: 10, 40, 50
  assert_int_equal(arrlen(updated_view->gm_ids), 3);

  // Verify the contents of gm_ids -> array should be sorted
  assert_int_equal(updated_view->gm_ids[0], 10);
  assert_int_equal(updated_view->gm_ids[1], 40);
  assert_int_equal(updated_view->gm_ids[2], 50);

  // Clean up
  view_free(updated_view);
}

// Main function
int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_next_view_add_gmid),
      cmocka_unit_test(test_next_view_remove_gmid),
      cmocka_unit_test(test_next_view_remove_nonexistent_gmid),
      cmocka_unit_test(test_next_view_multiple_changes),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
