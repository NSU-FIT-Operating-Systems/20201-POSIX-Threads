#include "common/error.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#ifdef BACKTRACE_ENABLED
#include <backtrace.h>
#include <backtrace-supported.h>
#endif

struct backtrace {
    backtrace_t *parent;
    uintptr_t pc;
    char *file;
    int lineno;
    char *func;
};

#if defined(BACKTRACE_SUPPORTED) && defined(BACKTRACE_ENABLED)
static pthread_once_t backtrace_state_init = PTHREAD_ONCE_INIT;

static struct backtrace_state *backtrace_state = NULL;

static void backtrace_init_on_fail(void *, const char *msg, int) {
    log_printf(LOG_ERR, "Could not setup the backtrace library: %s", msg);
}

static void initialize_backtrace_inner(void) {
    backtrace_state = backtrace_create_state(NULL, 1, backtrace_init_on_fail, NULL);
}

static void initialize_backtrace(void) {
    pthread_once(&backtrace_state_init, initialize_backtrace_inner);
}

struct backtrace_cb_data {
    backtrace_t **outermost;
    bool success;
};

static int backtrace_on_captured(
    void *data_opaque,
    uintptr_t pc,
    char const *file,
    int lineno,
    char const *func
) {
    struct backtrace_cb_data *data = data_opaque;
    backtrace_t *frame = malloc(sizeof(backtrace_t));
    if (frame == NULL) goto fail;

    *data->outermost = frame;
    *frame = (backtrace_t) {
        .pc = pc,
        .file = file == NULL ? NULL : strdup(file),
        .lineno = lineno,
        .func = func == NULL ? NULL : strdup(func),
    };

    data->outermost = &frame->parent;

    return 0;

fail:
    data->success = false;

    return 1;
}

static void backtrace_on_fail(void *data_opaque, char const *msg, int) {
    ((struct backtrace_cb_data *) data_opaque)->success = false;
    log_printf(LOG_WARN, "Could not capture the backtrace: %s", msg);
}

backtrace_t *backtrace_capture(void) {
    initialize_backtrace();

    backtrace_t *root = NULL;

    struct backtrace_cb_data data = {
        .outermost = &root,
        .success = true,
    };

    backtrace_full(backtrace_state, 0, backtrace_on_captured, backtrace_on_fail, &data);

    if (!data.success) {
        log_printf(LOG_DEBUG, "Capturing the stacktrace failed");
        backtrace_free(&root);
        root = NULL;
    }

    return root;
}

#else

backtrace_t *backtrace_capture(void) {
    return NULL;
}

#endif

static void backtrace_format_frame(
    backtrace_t const *frame,
    char const *prefix,
    string_t *buf
) {
    char const *file = frame->file;
    file = file == NULL ? file : "<unknown file>";

    string_appendf(buf, "%s%s", prefix, file);

    if (frame->lineno != 0) {
        string_appendf(buf, ":%d", frame->lineno);
    }

    if (frame->func != NULL) {
        string_appendf(buf, " (in function %s)", frame->func);
    } else {
        string_appendf(buf, " (in function <unknown>)");
    }
}

void backtrace_format(
    backtrace_t const *backtrace,
    char const *prefix,
    string_t *buf
) {
    prefix = prefix == NULL ? "" : prefix;

    for (backtrace_t const *frame = backtrace; frame != NULL; frame = frame->parent) {
        backtrace_format_frame(frame, prefix, buf);
    }
}

void backtrace_free(backtrace_t **backtrace) {
    backtrace_t *frame = *backtrace;

    if (frame == NULL) {
        return;
    }

    backtrace_free(&frame->parent);
    free(frame->file);
    free(frame->func);
    free(frame);
}
