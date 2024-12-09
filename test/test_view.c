#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "view.h"

// TODO: fix tests for new interface
// WARN: Olá

static void test_next_view(void **state) {
  (void)state;

  // Test case 1: Add a new GMID
  view_t previous = {0, NULL};
  arrput(previous.gm_ids, 10);
  view_change_t changes1[] = {{7, V_ADD}};

  view_t *next = result_unwrap(next_view(&previous, changes1));
  assert_non_null(next);
  assert_int_equal(next->id, 1);
  assert_int_equal(arrlen(next->gm_ids), 2);
  assert_int_equal(next->gm_ids[0], 10);
  assert_int_equal(next->gm_ids[1], 7);

  // Test case 2: Remove an existing GMID
  view_change_t changes2[] = {{7, V_DROP}};

  next = result_unwrap(next_view(next, changes2));
  assert_non_null(next);
  assert_int_equal(next->id, 2);
  assert_int_equal(arrlen(next->gm_ids), 1);
  assert_int_equal(next->gm_ids[0], 10);

  // Test case 3: Try to remove a non-existent GMID
  view_change_t changes3[] = {{42, V_DROP}};

  Result *result = next_view(next, changes3);
  assert_non_null(result);
  assert_true(result_is_err(result));
  result_free(result);

  free(next);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_next_view),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
