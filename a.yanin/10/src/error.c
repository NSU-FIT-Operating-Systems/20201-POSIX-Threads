#include "error.h"

#include <assert.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include <common/error-codes/display.h>

static err_t err_from_success(char const *file, int line) {
    return (err_t) {
        .context = NULL,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_SUCCESS,
    };
}

err_t err_from_bool(bool result, char const *context, char const *file, int line) {
    if (result) {
        return err_from_success(file, line);
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_BOOL
    };
}

err_t err_from_common_error_code(
    common_error_code_t code,
    char const *context,
    char const *file,
    int line
) {
    if (code == COMMON_ERROR_CODE_OK) {
        return err_from_success(file, line);
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_COMMON,
        .details.common.code = code,
    };
}

err_t err_from_errno(err_errno_t errno_code, char const *context, char const *file, int line) {
    int errno_int = (int) errno_code;

    if (errno_int == 0) {
        return err_from_success(file, line);
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_ERRNO,
        .details.errno_code.code = errno_int,
    };
}

err_t err_from_err(err_t err, char const *context, char const *file, int line) {
    if (!ERR_FAILED(err)) {
        return err_from_success(file, line);
    }

    err_t *cause = malloc(sizeof(err_t));

    if (cause != NULL) {
        *cause = err;
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_ERR,
        .details.err.cause = cause,
    };
}

err_t err_from_posix_err(posix_err_t err, char const *context, char const *file, int line) {
    if (err.errno_code == 0) {
        return err_from_success(file, line);
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_COMMON_POSIX,
        .details.common_posix = err,
    };
}

err_t err_from_gai(err_gai_t code, char const *context, char const *file, int line) {
    if (code == 0) {
        return err_from_success(file, line);
    }

    return (err_t) {
        .context = context,
        .file = file,
        .line = line,
        .variant = ERR_VARIANT_GAI,
        .details.gai.code = (int) code,
    };
}

static void err_print_success(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_SUCCESS);

    fputs(": success", stream);
}

static void err_print_common(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_COMMON);

    fputs(": ", stream);
    fputs(common_error_code_as_str(err->details.common.code), stream);
}

static void err_print_common_posix(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_COMMON_POSIX);

    fputs(": ", stream);

    if (err->details.common_posix.message != NULL) {
        fputs(err->details.common_posix.message, stream);
    } else {
        fputs("an error has occured", stream);
    }

    fprintf(
        stream, " ([errno %d] %s)",
        err->details.common_posix.errno_code, strerror(err->details.common_posix.errno_code)
    );
}

static void err_print_errno(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_ERRNO);

    fprintf(
        stream, ": [errno %d] %s",
        err->details.errno_code.code, strerror(err->details.errno_code.code)
    );
}

static void err_print_gai(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_GAI);

    fprintf(stream, ": %s", gai_strerror(err->details.gai.code));
}

static void err_print_err(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_ERR);

    if (err->context != NULL) {
        fprintf(stream, ": %s", err->context);
    }
}

static void err_print_err_stack_trace(FILE *stream, err_t const *err) {
    assert(stream != NULL);
    assert(err->variant == ERR_VARIANT_ERR);

    if (err->details.err.cause != NULL) {
        fputs("\nThe error above was caused by the following error:\n", stream);

        err_print(stream, err->details.err.cause, true);
    }
}

static void err_free_err(err_t *err) {
    assert(err != NULL);
    assert(err->variant == ERR_VARIANT_ERR);

    if (err->details.err.cause != NULL) {
        err_free(err->details.err.cause);
        free(err->details.err.cause);
        err->details.err.cause = NULL;
    }
}

void err_free(err_t *err) {
    assert(err != NULL);

    switch (err->variant) {
    case ERR_VARIANT_SUCCESS:
    case ERR_VARIANT_BOOL:
    case ERR_VARIANT_COMMON:
    case ERR_VARIANT_COMMON_POSIX:
    case ERR_VARIANT_ERRNO:
    case ERR_VARIANT_GAI:
        break;

    case ERR_VARIANT_ERR:
        err_free_err(err);

        break;
    }
}

void err_print(FILE *stream, err_t const *err, bool verbose) {
    assert(stream != NULL);
    assert(err != NULL);

    if (err->variant != ERR_VARIANT_ERR && err->context != NULL) {
        fprintf(stream, "%s", err->context);
    } else {
        fputs("An error has occurred", stream);
    }

    switch (err->variant) {
    case ERR_VARIANT_SUCCESS:
        err_print_success(stream, err);
        break;

    case ERR_VARIANT_BOOL:
        break;

    case ERR_VARIANT_COMMON:
        err_print_common(stream, err);
        break;

    case ERR_VARIANT_COMMON_POSIX:
        err_print_common_posix(stream, err);
        break;

    case ERR_VARIANT_ERRNO:
        err_print_errno(stream, err);
        break;

    case ERR_VARIANT_GAI:
        err_print_gai(stream, err);
        break;

    case ERR_VARIANT_ERR:
        err_print_err(stream, err);
        break;
    }

    fputs(".\n", stream);

    if (verbose) {
        if (err->file != NULL) {
            fprintf(stream, "\tat %s:%d\n", err->file, err->line);
        }

        if (err->variant == ERR_VARIANT_ERR) {
            err_print_err_stack_trace(stream, err);
        }
    }
}

void err_log_free(log_level_t level, err_t *err) {
    log_write(level, "");
    err_print(stderr, err, true);
    err_free(err);
}

void err_log_printf_free(log_level_t level, err_t *err, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    log_vwritef(level, fmt, args);
    fputs(": ", stderr);
    err_print(stderr, err, true);
    err_free(err);
}
