#ifndef _RESULT_H_
#define _RESULT_H_

#include <stdbool.h>

struct Result {
  // flags whether result is error or not
  bool _is_ok;

  // holds the value if result is ok
  void *ok;
  // holds error msg if failure
  const char *err;
};

typedef struct Result Result;

// allocates a new Result, sets ->value to the given value
Result *result_new_ok(void *value);

// allocates a new Result, ->error to the given error
Result *result_new_err(const char *error);

// returns the value of the Result and destroys it. If the Result contains an
// error the error message is printed to stderr and the process exists with
// EXIT_FAILURE
void *result_unwrap(Result *r);

// returns the value of the Result and destroys it. If the Result contains an
// error the provided error message is printed to stderr and the process exists
// with EXIT_FAILURE
void *result_expect(Result *r, const char *err);

void result_free(Result *r);

inline bool result_is_ok(Result *r) { return r->_is_ok; }
inline bool result_is_err(Result *r) { return !r->_is_ok; }

#endif
