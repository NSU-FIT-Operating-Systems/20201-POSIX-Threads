#include "common/loop/loop.h"

#include <common/error-codes/adapter.h>

#include "common/posix/adapter.h"
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
    arc_handler_t *handler;
    poll_flags_t flags;
} pollfd_meta_t;

typedef struct {
    loop_t *loop;
    arc_handler_t *handler;
    poll_flags_t flags;
} task_ctx_t;

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
    handler->status = LOOP_HANDLER_UNREGISTERED;
}

static error_t *loop_process_registrations(loop_t *self) {
    error_t *err = NULL;

    assert_mutex_lock(&self->pending_mtx);

    for (size_t i = 0; i < vec_handler_len(&self->pending_handlers); ++i) {
        arc_handler_t *handler = arc_handler_share(*vec_handler_get(&self->pending_handlers, i));
        loop_handler_status_t status = arc_handler_get(handler)->status;

        switch (status) {
        case LOOP_HANDLER_READY:
            break;

        case LOOP_HANDLER_QUEUED:
            log_abort("One of the pending handlers has QUEUED status (already added?)");

        case LOOP_HANDLER_UNREGISTERED:
            // skip this handler
            arc_handler_free(handler);

            continue;
        }

        err = error_from_common(vec_handler_push(&self->handlers, handler));
        if (err) goto push_fail;
    }

push_fail:
    for (size_t i = 0; i < vec_handler_len(&self->pending_handlers); ++i) {
        arc_handler_free(*vec_handler_get(&self->pending_handlers, i));
    }

    vec_handler_clear(&self->pending_handlers);

    assert_mutex_unlock(&self->pending_mtx);

    size_t handler_count = vec_handler_len(&self->handlers);

    for (size_t j = 0; j < handler_count; ++j) {
        size_t i = handler_count - j - 1;
        arc_handler_t *handler = *vec_handler_get(&self->handlers, i);

        if (arc_handler_get(handler)->status == LOOP_HANDLER_UNREGISTERED) {
            arc_handler_free(handler);
            vec_handler_remove(&self->handlers, i);
        }
    }

    return err;
}

static error_t *loop_prepare_pollfd_process_handler(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta,
    arc_handler_t *handler_arc
) {
    (void) self;

    error_t *err = NULL;
    handler_t *handler = arc_handler_get(handler_arc);
    handler_lock(handler);

    poll_flags_t flags = handler->current_flags = handler->pending_flags & LOOP_ALL_IN;
    err = error_from_common(vec_pollfd_push(pollfd, (struct pollfd) {
        .events = (short) flags,
        .fd = handler->fd,
    }));
    if (err) goto pollfd_fail;

    arc_handler_t *handler_arc_shared = arc_handler_share(handler_arc);
    err = error_from_common(vec_pollfd_meta_push(meta, (pollfd_meta_t) {
        .handler = handler_arc_shared,
    }));
    if (err) goto meta_fail;
    handler_arc_shared = NULL;

meta_fail:
    arc_handler_free(handler_arc_shared);

pollfd_fail:
    handler_unlock(handler);

    return err;
}

static error_t *loop_prepare_pollfd(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta,
    bool *empty
) {
    error_t *err = NULL;

    if (self->stopped) {
        *empty = true;

        return err;
    }

    for (size_t i = 0; i < vec_handler_len(&self->handlers); ++i) {
        arc_handler_t *handler = *vec_handler_get(&self->handlers, i);

        if (arc_handler_get(handler)->status != LOOP_HANDLER_READY) {
            continue;
        }

        err = loop_prepare_pollfd_process_handler(self, pollfd, meta, handler);
        if (err) goto fail;

        if (!arc_handler_get(handler)->passive) {
            *empty = false;
        }
    }

fail:
    return err;
}

static error_t *loop_poll(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta
) {
    (void) self;

    // use an infinite timeout for now; later, if we need timers, we'll have to adjust this
    int timeout_ms = -1;
    int poll_count = 0;
    error_t *err = error_wrap("Could not await I/O events", error_from_posix(wrapper_poll(
        vec_pollfd_as_ptr_mut(pollfd),
        vec_pollfd_len(pollfd),
        timeout_ms,
        &poll_count
    )));
    if (err) goto fail;

    int examined_active = 0;

    for (size_t i = 0; examined_active < poll_count && i < vec_pollfd_len(pollfd); ++i) {
        struct pollfd const *entry = vec_pollfd_get(pollfd, i);

        if (entry->revents == 0) {
            continue;
        }

        error_assert(error_wrap("A file descriptor managed by a handler was closed",
            OK_IF((entry->revents & POLLNVAL) == 0)));

        pollfd_meta_t *meta_entry = vec_pollfd_meta_get_mut(meta, i);

        ++examined_active;
        poll_flags_t flags = entry->revents & LOOP_ALL;

        handler_t *handler = arc_handler_get(meta_entry->handler);
        handler_lock(handler);
        flags &= handler->current_flags | (LOOP_HUP | LOOP_ERR);
        handler_unlock(handler);

        meta_entry->flags = flags;
    }

fail:
    return err;
}

static error_t *loop_handler_task_cb(task_ctx_t *ctx) {
    handler_t *handler = arc_handler_get(ctx->handler);

    handler_lock(handler);
    error_t *err = handler->vtable->process(handler, ctx->loop, ctx->flags);
    handler_unlock(handler);

    if (err) {
        if (handler->vtable->on_error != NULL) {
            err = error_wrap("A handler's on_error method has returned an error",
                handler->vtable->on_error(handler, err));
        } else {
            err = error_wrap("A handler has returned an error", err);
        }
    }

    if (err) {
        assert_mutex_lock(&ctx->loop->pending_mtx);
        error_t *push_err = error_from_common(vec_error_push(&ctx->loop->errors, err));
        assert_mutex_unlock(&ctx->loop->pending_mtx);

        if (push_err) {
            err = error_combine(push_err, err);
        } else {
            err = NULL;
        }
    }

    atomic_compare_exchange_strong(
        &handler->status,
        &(loop_handler_status_t) { LOOP_HANDLER_QUEUED },
        LOOP_HANDLER_READY
    );
    arc_handler_free(ctx->handler);
    free(ctx);

    return err;
}

static error_t *loop_dispatch_task(loop_t *self, pollfd_meta_t *meta_entry) {
    error_t *err = NULL;

    task_ctx_t *ctx = malloc(sizeof(task_ctx_t));
    err = OK_IF(ctx != NULL);
    if (err) goto malloc_fail;

    *ctx = (task_ctx_t) {
        .loop = self,
        .handler = arc_handler_share(meta_entry->handler),
        .flags = meta_entry->flags,
    };

    handler_t *handler = arc_handler_get(meta_entry->handler);
    atomic_compare_exchange_strong(
        &handler->status,
        &(loop_handler_status_t) { LOOP_HANDLER_READY },
        LOOP_HANDLER_QUEUED
    );
    executor_submission_t status = executor_submit(self->executor, (task_t) {
        .cb = (task_cb_t) loop_handler_task_cb,
        .data = (void *) ctx,
    });

    switch (status) {
    case EXECUTOR_SUBMITTED:
        break;

    case EXECUTOR_DROPPED:
        err = error_from_cstr("A handler task has been rejected by the executor", NULL);
        goto submit_fail;
    }

    return err;

submit_fail:
    arc_handler_free(ctx->handler);
    free(ctx);

malloc_fail:
    return err;
}

static error_t *loop_submit_tasks(loop_t *self, vec_pollfd_meta_t *meta) {
    TODO("create tasks for each handler and submit them to the executor");

    error_t *err = NULL;

    for (size_t i = 0; i < vec_pollfd_meta_len(meta); ++i) {
        pollfd_meta_t *meta_entry = vec_pollfd_meta_get_mut(meta, i);

        if (meta_entry->flags == 0) {
            continue;
        }

        err = loop_dispatch_task(self, meta_entry);
        if (err) goto fail;
    }

fail:
    return err;
}

static error_t *loop_process_task_results(loop_t *self) {
    error_t *err = NULL;

    assert_mutex_lock(&self->error_mtx);

    for (size_t i = 0; i < vec_error_len(&self->errors); ++i) {
        err = error_combine(err, *vec_error_get(&self->errors, i));
    }

    vec_error_clear(&self->errors);

    assert_mutex_unlock(&self->error_mtx);

    return err;
}

error_t *loop_run(loop_t *self) {
    error_t *err = NULL;

    vec_pollfd_t pollfd = vec_pollfd_new();
    vec_pollfd_meta_t meta = vec_pollfd_meta_new();

    log_printf(LOG_DEBUG, "Starting the event loop");

    while (true) {
        err = loop_process_registrations(self);
        if (err) goto fail;

        vec_pollfd_clear(&pollfd);

        for (size_t i = 0; i < vec_pollfd_meta_len(&meta); ++i) {
            arc_handler_free(vec_pollfd_meta_get(&meta, i)->handler);
        }

        vec_pollfd_meta_clear(&meta);

        bool empty = false;
        err = loop_prepare_pollfd(self, &pollfd, &meta, &empty);
        if (err) goto fail;

        assert(vec_pollfd_len(&pollfd) == vec_pollfd_meta_len(&meta));

        if (empty) {
            log_printf(LOG_DEBUG, "Shutting down the event loop: 0 active handlers");
            break;
        }

        err = loop_poll(self, &pollfd, &meta);
        if (err) goto fail;

        err = loop_submit_tasks(self, &meta);
        if (err) goto fail;

        err = loop_process_task_results(self);
        if (err) goto fail;
    }

fail:
    for (size_t i = 0; i < vec_pollfd_meta_len(&meta); ++i) {
        arc_handler_t *handler = vec_pollfd_meta_get(&meta, i)->handler;
        arc_handler_get(handler)->status = LOOP_HANDLER_UNREGISTERED;
        arc_handler_free(handler);
    }

    vec_pollfd_meta_free(&meta);
    vec_pollfd_free(&pollfd);

    return err;
}

void loop_stop(loop_t *self) {
    self->stopped = true;
}
