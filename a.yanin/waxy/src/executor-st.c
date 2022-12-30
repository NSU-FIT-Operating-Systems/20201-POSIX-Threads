#include "executor.h"

#include <common/executor/single.h>

error_t *create_default_executor(executor_t **result) {
    error_t *err = NULL;

    executor_single_t *executor = NULL;
    err = executor_single_new("Waxy", &executor);

    if (err) {
        return err;
    }

    *result = (executor_t *) executor;

    return err;
}
