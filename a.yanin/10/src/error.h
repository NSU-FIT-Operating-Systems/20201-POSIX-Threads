#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <common/error-codes/error-codes.h>
#include <common/posix/error.h>
#include <common/log/log.h>

typedef enum {
    ERR_ERRNO_OK = 0,
} err_errno_t;

typedef enum {
    ERR_GAI_OK = 0,
} err_gai_t;

typedef enum {
    ERR_VARIANT_SUCCESS,
    ERR_VARIANT_BOOL,
    ERR_VARIANT_COMMON,
    ERR_VARIANT_COMMON_POSIX,
    ERR_VARIANT_ERRNO,
    ERR_VARIANT_GAI,
    ERR_VARIANT_ERR,
} err_variant_t;

typedef struct err err_t;

struct err {
    char const *context;
    char const *file;
    int line;

    err_variant_t variant;

    union {
        struct {
            common_error_code_t code;
        } common;

        posix_err_t common_posix;

        struct {
            int code;
        } errno_code;

        struct {
            err_t *cause;
        } err;

        struct {
            int code;
        } gai;
    } details;
};

#define ERR(FALLIBLE, CONTEXT) (_Generic((FALLIBLE), \
        bool: err_from_bool, \
        common_error_code_t: err_from_common_error_code, \
        err_errno_t: err_from_errno, \
        posix_err_t: err_from_posix_err, \
        err_gai_t: err_from_gai, \
        err_t: err_from_err \
    )((FALLIBLE), (CONTEXT), __FILE__, __LINE__))

#define OK ((err_t) { \
        .context = NULL, \
        .file = __FILE__, \
        .line = __LINE__, \
        .variant = ERR_VARIANT_SUCCESS, \
    })

#define ERR_FAILED(ERR) ((ERR).variant != ERR_VARIANT_SUCCESS)

#define ERR_ASSERT(ERR) do { \
        err_t fallible = (ERR); \
        if (ERR_FAILED(fallible)) { \
            err_log_printf_free(LOG_ERR, &fallible, "Assertion failed"); \
            abort(); \
        } \
    } while (false)

err_t err_from_bool(bool result, char const *context, char const *file, int line);
err_t err_from_common_error_code(
    common_error_code_t code,
    char const *context,
    char const *file,
    int line
);
err_t err_from_errno(err_errno_t errno_code, char const *context, char const *file, int line);
err_t err_from_err(err_t err, char const *context, char const *file, int line);
err_t err_from_posix_err(posix_err_t err, char const *context, char const *file, int line);
err_t err_from_gai(err_gai_t code, char const *context, char const *file, int line);

void err_free(err_t *err);
void err_print(FILE *stream, err_t const *err, bool verbose);
void err_log_free(log_level_t level, err_t *err);

[[gnu::format(printf, 3, 4)]]
void err_log_printf_free(log_level_t level, err_t *err, char const *fmt, ...);
