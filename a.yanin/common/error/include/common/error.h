#pragma once

#include <common/collections/string.h>
#include <common/log/log.h>

// The verbosity options.
//
// Values can be combined with `|`
// (e.g., `ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE`)
typedef enum {
    // Only prints the description of the top-level error.
    ERROR_VERBOSITY_SHORT = 0,

    // Prints the chain of errors with their descriptions.
    ERROR_VERBOSITY_SOURCE_CHAIN = 1 << 0,

    // Prints the backtraces.
    ERROR_VERBOSITY_BACKTRACE = 1 << 1,
} error_verbosity_t;

typedef struct error error_t;
typedef struct backtrace backtrace_t;

typedef error_t const *(*error_vtable_source_t)(error_t const *self);
typedef void (*error_vtable_description_t)(error_t const *self, string_t *buf);
typedef void (*error_vtable_free_t)(error_t *self);

typedef struct {
    // The primary source of the error.
    //
    // Returns `NULL` if not available.
    error_t const *(*source)(error_t const *self);

    // The secondary source of the error.
    //
    // Returns `NULL` if not available.
    error_t const *(*secondary)(error_t const *self);

    // Writes out a description of the error to `buf`.
    // This function pointer can be `NULL` if the error is synthetic and should not be shown.
    // The primary source is then used for printing the description and the secondary source is
    // displayed after the primary's secondary.
    void (*description)(error_t const *self, string_t *buf);

    // Frees the resources associated with this error.
    void (*free)(error_t *self);
} error_vtable_t;

// The main error struct.
//
// Concrete error implementations must include this as the first member of their struct to allow
// up- and downcasting without invoking undefined behavior.
//
// Errors are always passed via a pointer.
// A `NULL` pointer represents a successful execution.
// Therefore, `error_t *` can be used as a return type for fallible functions.
//
// `error_t` is dynamically allocated and must be freed via `error_free` after it's no longer
// necessary.
struct error {
    error_vtable_t const *vtable;
    backtrace_t *backtrace;
};

// A special error value that's returned if an error could not be constructed.
extern error_t sentinel_error;

// Initializes `self` with the given `vtable`.
//
// Fills the backtrace if possible.
void error_init(error_t *self, error_vtable_t const *vtable);

// A convenience function that the `source` and `secondary` entries of the vtable can be set to so
// that they always return a `NULL.
error_t const *error_null(error_t const *self);

error_t *error_from_cstr(char const *str, error_t *source);
error_t *error_from_string(string_t str, error_t *source);
error_t *error_ok_if(bool success, char const *expr);

#define OK_IF(EXPR) error_ok_if((bool)(EXPR), #EXPR)

// Creates a new error that has `primary` as its primary source and `secondary` as its
// secondary source.
error_t *error_combine(error_t *primary, error_t *secondary);

// If `*self != NULL`, frees `*self` and its associated resources and sets `*self` to `NULL`.
// Otherwise does nothing.
void error_free(error_t **self);

// Writes the error to a string.
//
// The `verbosity` options control what data is printed to the string.
void error_format(error_t const *self, error_verbosity_t verbosity, string_t *buf);

// Logs the error with the specified `log_level` and frees it.
void error_log_free(error_t **self, log_level_t log_level, error_verbosity_t verbosity);

// Asserts the error is `NULL`.
//
// If the assertion fails, logs the error message and aborts the program.
void error_assert(error_t *self);

// Captures the current backtrace.
//
// Returns `NULL` on failure.
backtrace_t *backtrace_capture(void);

// Prints a backtrace to `buf`.
void backtrace_format(
    backtrace_t const *backtrace,
    char const *prefix,
    string_t *buf
);

void backtrace_free(backtrace_t **backtrace);
