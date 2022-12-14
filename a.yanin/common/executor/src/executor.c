#include <common/executor/executor.h>

#include <stdlib.h>

void executor_init(executor_t *self, executor_vtable_t const *vtable) {
    self->vtable = vtable;
}

void executor_free(executor_t *self) {
    if (self == NULL) return;

    self->vtable->free(self);
    free(self);
}

executor_submission_t executor_submit(executor_t *self, task_t task) {
    return self->vtable->submit(self, task);
}

void executor_shutdown(executor_t *self) {
    self->vtable->shutdown(self);
}

void executor_await_termination(executor_t *self) {
    self->vtable->await_termination(self);
}
