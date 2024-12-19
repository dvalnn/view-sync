#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "result.h"
#include "vector_clock.h"

static void test_vc_init(void **state) {
  (void)state;

  Result *res = vc_init(10);
  assert_false(result_is_err(res));
  result_free(res);
}

static void test_vc_inc(void **state) {
  (void)state;

  vector_clock_t *vc = result_unwrap(vc_init(10));
  vc_inc(vc, 6);
  assert_int_equal(vc->clock[6], 1);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_vc_init),
      cmocka_unit_test(test_vc_inc),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
