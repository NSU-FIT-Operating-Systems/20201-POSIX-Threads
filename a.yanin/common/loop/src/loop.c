#include "common/loop/loop.h"

#include <stdatomic.h>

#include <common/error-codes/adapter.h>
#include <common/posix/adapter.h>

#include "common/loop/notify.h"
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
    bool forced;
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

struct loop {
    vec_handler_t handlers;
    pthread_mutex_t pending_mtx;
    vec_handler_t pending_handlers;
    pthread_mutex_t error_mtx;
    vec_error_t errors;
    executor_t *executor;
    // this is a weak reference to `notify`
    notify_t *notify;
    atomic_bool stopped;
    // whether this loop was ever running
    bool started;
};

static error_t *loop_on_notified(loop_t *, notify_t *) {
    return NULL;
}

error_t *loop_new(executor_t *executor, loop_t **result) {
    error_t *err = NULL;

    loop_t *self = calloc(1, sizeof(loop_t));
    err = error_wrap("Could not allocate memory for the loop", OK_IF(self != NULL));
    if (err) goto calloc_fail;

    self->handlers = vec_handler_new();
    self->pending_handlers = vec_handler_new();
    self->errors = vec_error_new();
    self->executor = executor;
    self->stopped = false;

    err = error_wrap("Could not initialize the notification mechanism", notify_new(&self->notify));
    if (err) goto notify_fail;

    // even though we don't need to do anything when notified,
    // we have to set the callback to set LOOP_READ on `self->notify`
    notify_set_cb(self->notify, loop_on_notified);

    ((handler_t *) self->notify)->passive = true;
    arc_handler_t *notify = arc_handler_new((handler_t *) self->notify);
    vec_handler_push(&self->pending_handlers, notify);

    pthread_mutexattr_t mtx_attr;
    error_assert(error_wrap("Could not initialize mutex attributes",
        error_from_errno(pthread_mutexattr_init(&mtx_attr))));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    error_assert(error_wrap("Could not create a mutex", error_from_errno(
        pthread_mutex_init(&self->pending_mtx, &mtx_attr))));
    error_assert(error_wrap("Could not create a mutex", error_from_errno(
        pthread_mutex_init(&self->error_mtx, &mtx_attr))));

    pthread_mutexattr_destroy(&mtx_attr);

    *result = self;

    return err;

notify_fail:
    free(self);

calloc_fail:
    return err;
}

void loop_free(loop_t *self) {
    error_assert(error_wrap("The loop must have been stopped",
        OK_IF(!self->started || self->stopped)));

    for (size_t i = 0; i < vec_handler_len(&self->pending_handlers); ++i) {
        arc_handler_free(*vec_handler_get(&self->pending_handlers, i));
    }

    for (size_t ri = 0; ri < vec_handler_len(&self->handlers); ++ri) {
        size_t i = vec_handler_len(&self->handlers) - ri - 1;

        arc_handler_t *handler = *vec_handler_get(&self->handlers, i);
        arc_handler_free(handler);
    }

    for (size_t i = 0; i < vec_error_len(&self->errors); ++i) {
        error_free(vec_error_get_mut(&self->errors, i));
    }

    pthread_mutex_destroy(&self->error_mtx);
    pthread_mutex_destroy(&self->pending_mtx);

    vec_error_free(&self->errors);
    vec_handler_free(&self->pending_handlers);
    vec_handler_free(&self->handlers);

    free(self);
}

error_t *loop_register(loop_t *self, handler_t *handler) {
    assert_mutex_lock(&self->pending_mtx);

    error_t *err = NULL;
    bool pushed = false;

    arc_handler_t *arc = arc_handler_new(handler);
    err = OK_IF(arc != NULL);
    if (err) goto fail;

    err = error_wrap("Could not insert the handle in the pending queue", error_from_common(
        vec_handler_push(&self->pending_handlers, arc)));
    if (err) goto fail;

    pushed = true;
    handler->loop = self;

fail:
    assert_mutex_unlock(&self->pending_mtx);

    if (pushed) {
        loop_interrupt(self);
    }

    return err;
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
    loop_t *,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta,
    arc_handler_t *handler_arc,
    bool *has_forced_handlers
) {
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
    bool forced = atomic_exchange(&handler->force, false);
    err = error_from_common(vec_pollfd_meta_push(meta, (pollfd_meta_t) {
        .handler = handler_arc_shared,
        .forced = forced,
    }));
    if (err) goto meta_fail;
    handler_arc_shared = NULL;

    if (forced) {
        *has_forced_handlers = true;
    }

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
    bool *empty,
    bool *has_forced_handlers
) {
    error_t *err = NULL;

    if (self->stopped) {
        log_printf(LOG_DEBUG, "The loop has been stopped");
        *empty = true;

        return err;
    }

    for (size_t i = 0; i < vec_handler_len(&self->handlers); ++i) {
        arc_handler_t *arc = *vec_handler_get(&self->handlers, i);
        handler_t *handler = arc_handler_get(arc);
        loop_handler_status_t status = handler->status;

        if (status == LOOP_HANDLER_UNREGISTERED) {
            continue;
        }

        if (!handler->passive) {
            *empty = false;
        }

        if (status == LOOP_HANDLER_QUEUED) {
            continue;
        }

        err = loop_prepare_pollfd_process_handler(self, pollfd, meta, arc, has_forced_handlers);
        if (err) goto fail;
    }

fail:
    return err;
}

static error_t *loop_poll(
    loop_t *self,
    vec_pollfd_t *pollfd,
    vec_pollfd_meta_t *meta,
    bool has_forced_handlers
) {
    (void) self;

    // use an infinite timeout for now; later, if we need timers, we'll have to adjust this
    int timeout_ms = -1;

    if (has_forced_handlers) {
        timeout_ms = 0;
    }

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
    error_t *err = NULL;

    handler_t *handler = arc_handler_get(ctx->handler);

    handler_lock(handler);
    err = handler->vtable->process(handler, ctx->loop, ctx->flags);
    handler_unlock(handler);

    if (err) {
        if (handler->vtable->on_error != NULL) {
            err = error_wrap("A handler's on_error method has returned an error",
                handler->vtable->on_error(handler, ctx->loop, err));
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

    if (handler != (handler_t *) ctx->loop->notify) {
        loop_interrupt(ctx->loop);
    }

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

    if (handler == (handler_t *) self->notify) {
        err = loop_handler_task_cb(ctx);
    } else {
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
    }

    return err;

submit_fail:
    arc_handler_free(ctx->handler);
    free(ctx);

malloc_fail:
    return err;
}

static error_t *loop_submit_tasks(loop_t *self, vec_pollfd_meta_t *meta) {
    error_t *err = NULL;

    for (size_t i = 0; i < vec_pollfd_meta_len(meta); ++i) {
        pollfd_meta_t *meta_entry = vec_pollfd_meta_get_mut(meta, i);

        if (meta_entry->flags == 0 && !meta_entry->forced) {
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
        error_t *task_err = *vec_error_get(&self->errors, i);

        if (i > 0) {
            err = error_combine(err, error_wrap("Another handler has failed", task_err));
        } else {
            err = error_combine(err, task_err);
        }
    }

    vec_error_clear(&self->errors);

    assert_mutex_unlock(&self->error_mtx);

    return err;
}

error_t *loop_run(loop_t *self) {
    error_t *err = NULL;

    self->started = true;

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
        bool has_forced_handlers = false;
        err = loop_prepare_pollfd(self, &pollfd, &meta, &empty, &has_forced_handlers);
        if (err) goto fail;

        assert(vec_pollfd_len(&pollfd) == vec_pollfd_meta_len(&meta));

        if (empty) {
            log_printf(LOG_DEBUG, "Shutting down the event loop: 0 active handlers");
            break;
        }

        err = loop_poll(self, &pollfd, &meta, has_forced_handlers);
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
    notify_wakeup(self->notify);
}

void loop_interrupt(loop_t *self) {
    notify_post(self->notify);
}
