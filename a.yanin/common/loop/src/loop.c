#include "common/loop/loop.h"

#include <common/error-codes/adapter.h>

#include "util.h"

#define ARC_LABEL handler
#define ARC_ELEMENT_TYPE handler_t
#define ARC_CONFIG (COLLECTION_DEFINE)
#define ARC_FREE_CB handler_free
#include <common/memory/arc.h>

#define VEC_LABEL handler
#define VEC_ELEMENT_TYPE arc_handler_ptr_t
#define VEC_CONFIG (COLLECTION_DEFINE)
#include <common/collections/vec.h>

#define VEC_LABEL pollfd
#define VEC_ELEMENT_TYPE struct pollfd
#define VEC_CONFIG (COLLECTION_DEFINE)
#include <common/collections/vec.h>

#define VEC_LABEL error
#define VEC_ELEMENT_TYPE errorp_t
#define VEC_CONFIG (COLLECTION_DEFINE)
#include <common/collections/vec.h>

typedef struct {
    handler_t *handler;
} pollfd_meta_t;

#define VEC_LABEL pollfd_meta
#define VEC_ELEMENT_TYPE pollfd_meta_t
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

void loop_init(loop_t *self, executor_t *executor) {
    self->handlers = vec_handler_new();
    self->pending_handlers = vec_handler_new();
    self->errors = vec_error_new();
    self->executor = executor;
    self->stopped = false;

    pthread_mutexattr_t mtx_attr;
    error_assert(error_wrap("Could not initialize mutex attributes",
        error_from_errno(pthread_mutexattr_init(&mtx_attr))));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    error_assert(error_wrap("Could not create a mutex", error_from_errno(
        pthread_mutex_init(&self->pending_mtx, &mtx_attr))));
    error_assert(error_wrap("Could not create a mutex", error_from_errno(
        pthread_mutex_init(&self->error_mtx, &mtx_attr))));

    pthread_mutexattr_destroy(&mtx_attr);
}

error_t *loop_register(loop_t *self, handler_t *handler) {
    assert_mutex_lock(&self->pending_mtx);

    error_t *err = NULL;

    arc_handler_t *arc = arc_handler_new(handler);
    err = OK_IF(arc != NULL);
    if (err) goto fail;

    err = error_wrap(
        "Could not insert the handle in the pending queue",
        error_from_common(
            vec_handler_push(&self->pending_handlers, arc)));
    if (err) goto fail;

fail:
    assert_mutex_unlock(&self->pending_mtx);

    return err;
}

void loop_unregister(loop_t *, handler_t *handler) {
    handler->active = false;
}

static error_t *loop_process_registrations(loop_t *self) {
    (void) self;
    TODO("add pending handlers unless they have already been unregistered");
    TODO("remove unregistered handlers");
    TODO("make sure to check self->stopped");
}

static error_t *loop_prepare_pollfd(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta
) {
    (void) self, (void) pollfd, (void) meta;
    TODO("for each ready handler, create entries in pollfd and meta");
    TODO("move pending flags to current");
    TODO("make sure to lock each handler before retrieving the mask");
}

static error_t *loop_poll(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta
) {
    (void) self, (void) pollfd, (void) meta;
    TODO("call poll(2)");
    TODO("exclude events that the handler didn't request, except for HUP/ERR");
    TODO("make sure to lock each handler when examining its mask");
}

static error_t *loop_submit_tasks(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta
) {
    (void) self, (void) pollfd, (void) meta;
    TODO("create tasks for each handler and submit them to the executor");
}

static error_t *loop_process_task_results(loop_t *self) {
    (void) self;
    TODO("check if any task has reported a failure (self->errors)");
    TODO("combine all the errors into one and return it");
}

error_t *loop_run(loop_t *self) {
    error_t *err = NULL;

    vec_pollfd_t pollfd = vec_pollfd_new();
    vec_pollfd_meta_t meta = vec_pollfd_meta_new();

    log_printf(LOG_DEBUG, "Starting the event loop");

    while (true) {
        err = loop_process_registrations(self);
        if (err) goto fail;

        err = loop_prepare_pollfd(self, &pollfd, &meta);
        if (err) goto fail;

        if (vec_pollfd_len(&pollfd) == 0) {
            log_printf(LOG_DEBUG,
                "Shutting down the event loop: 0 active handlers");
            break;
        }

        err = loop_poll(self, &pollfd, &meta);
        if (err) goto fail;

        err = loop_submit_tasks(self, &pollfd, &meta);
        if (err) goto fail;

        err = loop_process_task_results(self);
        if (err) goto fail;
    }

fail:
    vec_pollfd_meta_free(&meta);
    vec_pollfd_free(&pollfd);

    return err;
}

void loop_stop(loop_t *self) {
    self->stopped = true;
}
