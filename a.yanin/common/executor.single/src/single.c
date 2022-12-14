#include <common/executor/single.h>

#include <stdlib.h>

#include <common/collections/string.h>

struct executor_single {
    executor_t executor;
    char const *name;
    executor_single_on_error_cb_t on_error;
    bool shut;
};

void executor_single_free(executor_single_t *) {}

executor_submission_t executor_single_submit(executor_single_t *self, task_t task) {
    if (self->shut) {
        return EXECUTOR_DROPPED;
    }

    error_t *err = task.cb(task.data);

    if (err && self->on_error != NULL) {
        err = self->on_error(self, err);
    }

    if (err) {
        string_t buf;

        if (string_new(&buf) == COMMON_ERROR_CODE_OK) {
            error_format(err, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE, &buf);
            log_abort(
                "A task submitted to an executor `%s` has failed: %s",
                self->name,
                string_as_cptr(&buf)
            );
            string_free(&buf);
        } else {
            log_abort(
                "A task submitted to an executor `%s` has failed (could not render the error message)",
                self->name
            );
        }
    }

    return EXECUTOR_SUBMITTED;
}

void executor_single_shutdown(executor_single_t *self) {
    self->shut = true;
}

static executor_vtable_t const executor_single_vtable = {
    .free = (executor_vtable_free_t) executor_single_free,
    .submit = (executor_vtable_submit_t) executor_single_submit,
    .shutdown = (executor_vtable_shutdown_t) executor_single_shutdown,
};

error_t *executor_single_new(char const *name, executor_single_t **result) {
    error_t *err = NULL;

    executor_single_t *self = calloc(1, sizeof(executor_single_t));
    err = error_wrap("Could not allocate memory for the executor", OK_IF(self != NULL));
    if (err) goto calloc_fail;

    executor_init(&self->executor, &executor_single_vtable);
    self->name = name;
    self->on_error = NULL;
    self->shut = false;

    *result = self;

calloc_fail:
    return err;
}

void executor_single_on_error(executor_single_t *self, executor_single_on_error_cb_t on_error) {
    self->on_error = on_error;
}
