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

/**
 * @brief Creates a new successful `Result` object.
 *
 * Allocates and initializes a new `Result` structure representing a successful
 * operation. The `ok` field is set to the provided value, and the `err` field
 * is set to `nullptr`.
 *
 * @param value A pointer to the value to store in the `Result` object.
 * @return A pointer to the newly created `Result` object.
 */
Result *result_new_ok(void *value);

/**
 * @brief Creates a new error `Result` object.
 *
 * Allocates and initializes a new `Result` structure representing a failed
 * operation. The `err` field is set to the provided error message, and the
 * `ok` field is set to `nullptr`.
 *
 * @param error A string describing the error that occurred.
 * @return A pointer to the newly created `Result` object.
 */
Result *result_new_err(const char *error);

/**
 * @brief Extracts the value from a successful `Result` or exits on error.
 *
 * If the `Result` represents a success, the function extracts and returns the
 * `ok` value, freeing the `Result` in the process. If the `Result` is an error,
 * the function prints the error message to `stderr` and exits the program.
 *
 * @param r A pointer to the `Result` object to unwrap.
 * @return A pointer to the value stored in the `Result`'s `ok` field.
 * @note The function terminates the program on error.
 */
void *_result_unwrap(Result *r, const int line, const char *func,
                     const char *file);
#define result_unwrap(r) _result_unwrap(r, __LINE__, __FUNCTION__, __FILE__)

/**
 * @brief Extracts the value from a successful `Result` or exits with a custom
 * message.
 *
 * If the `Result` represents a success, the function extracts and returns the
 * `ok` value, freeing the `Result` in the process. If the `Result` is an error,
 * the function prints a custom error message along with the `Result`'s error
 * message to `stderr` and exits the program.
 *
 * @param r A pointer to the `Result` object to unwrap.
 * @param msg A custom message to display if the `Result` is an error.
 * @return A pointer to the value stored in the `Result`'s `ok` field.
 * @note The function terminates the program on error.
 */
void *_result_expect(Result *r, const char *msg, const int line,
                     const char *func, const char *file);
#define result_expect(r, msg)                                                  \
  _result_expect(r, msg, __LINE__, __FUNCTION__, __FILE__)
/**
 * @brief Frees the memory associated with a `Result` object.
 *
 * Releases the memory allocated for the `Result` structure. Does not free the
 * memory pointed to by the `ok` or `err` fields.
 *
 * @param r A pointer to the `Result` object to free.
 */
void result_free(Result *r);

inline bool result_is_ok(Result *r) { return r->_is_ok; }
inline bool result_is_err(Result *r) { return !r->_is_ok; }

#define RESULT_UNIMPLEMENTED result_unwrap(result_new_err("Unimplemented"))
#define RESULT_UNREACHABLE result_unwrap(result_new_err("Unreachable"))

#endif
