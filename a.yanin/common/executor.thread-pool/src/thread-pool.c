#include <common/executor/thread-pool.h>

#include <common/error-codes/adapter.h>

#include "util.h"

#define VEC_ELEMENT_TYPE pthread_t
#define VEC_LABEL pthread
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

#define DLIST_ELEMENT_TYPE task_t
#define DLIST_LABEL task
#define DLIST_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/dlist.h>

typedef enum {
    // The thread pool is initializing.
    //
    // If initialization fails, the state will be set to `THREAD_POOL_STOPPING`.
    THREAD_POOL_STARTING,

    // All the worker threads have been spawned, and the executor has not yet been shut down.
    THREAD_POOL_RUNNING,

    // The executor has been shut down (either on request or due to an internal failure).
    THREAD_POOL_STOPPING,
} thread_pool_state_t;

struct executor_thread_pool {
    executor_t executor;

    char const *pool_name;
    size_t size;
    vec_pthread_t threads;

    pthread_mutex_t mtx;
    pthread_cond_t cond;
    thread_pool_state_t state;
    dlist_task_t tasks;
    executor_thread_pool_on_error_cb_t on_error;
};

static void *worker_thread(void *data) {
    executor_thread_pool_t *ex = data;

    assert_mutex_lock(&ex->mtx);

    while (ex->state == THREAD_POOL_STARTING) {
        assert_cond_wait(&ex->cond, &ex->mtx);
    }

    // TODO: set the log prefix

    while (true) {
        while (ex->state == THREAD_POOL_RUNNING && dlist_task_len(&ex->tasks) == 0) {
            assert_cond_wait(&ex->cond, &ex->mtx);
        }

        if (ex->state != THREAD_POOL_RUNNING) {
            break;
        }

        task_t task = dlist_task_remove(&ex->tasks, dlist_task_head_mut(&ex->tasks));

        assert_mutex_unlock(&ex->mtx);
        error_t *err = task.cb(task.data);

        if (err) {
            assert_mutex_lock(&ex->mtx);
            executor_thread_pool_on_error_cb_t on_error = ex->on_error;
            assert_mutex_unlock(&ex->mtx);

            if (on_error != NULL) {
                err = error_wrap("The executor's on_error callback has returned an error",
                    on_error(ex, err, task));
            }
        }

        if (err) {
            string_t buf;

            if (string_new(&buf) == COMMON_ERROR_CODE_OK) {
                error_format(err, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE, &buf);
                log_abort("A task submitted to an executor `%s` has failed: %s",
                    ex->pool_name,
                    string_as_cptr(&buf)
                );
                string_free(&buf);
            } else {
                log_abort(
                    "A task submitted to an executor `%s` has failed (could not render the error message)",
                    ex->pool_name
                );
            }
        }

        assert_mutex_lock(&ex->mtx);
    }

    assert_mutex_unlock(&ex->mtx);

    return NULL;
}

static void executor_thread_pool_free(executor_thread_pool_t *self) {
    assert_mutex_lock(&self->mtx);
    self->state = THREAD_POOL_STOPPING;
    pthread_cond_broadcast(&self->cond);
    assert_mutex_unlock(&self->mtx);

    for (size_t i = 0; i < vec_pthread_len(&self->threads); ++i) {
        pthread_t thread_id = *vec_pthread_get(&self->threads, i);
        pthread_join(thread_id, NULL);
    }

    dlist_task_free(&self->tasks);
    pthread_cond_destroy(&self->cond);
    pthread_mutex_destroy(&self->mtx);
    vec_pthread_free(&self->threads);
}

static executor_submission_t executor_thread_pool_submit(
    executor_thread_pool_t *self,
    task_t task
) {
    error_t *err = NULL;

    assert_mutex_lock(&self->mtx);

    if (self->state != THREAD_POOL_RUNNING) {
        assert_mutex_unlock(&self->mtx);

        return EXECUTOR_DROPPED;
    }

    dlist_task_node_t *node = NULL;
    err = error_wrap("Could not add a task to the queue", error_from_common(
        dlist_task_append(&self->tasks, task, &node)));
    executor_thread_pool_on_error_cb_t on_error = self->on_error;

    if (!err && dlist_task_len(&self->tasks) == 1) {
        // the queue was empty; wake up somebody out there
        pthread_cond_signal(&self->cond);
    }

    assert_mutex_unlock(&self->mtx);

    if (err && on_error != NULL) {
        err = error_wrap("The executor's on_error callback has returned an error",
            on_error(self, err, task));

        error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_BACKTRACE | ERROR_VERBOSITY_SOURCE_CHAIN);
        log_printf(LOG_ERR, "Shutting down the executor");

        assert_mutex_lock(&self->mtx);
        self->state = THREAD_POOL_STOPPING;
        pthread_cond_broadcast(&self->cond);
        assert_mutex_unlock(&self->mtx);
    }

    return EXECUTOR_SUBMITTED;
}

static void executor_thread_pool_shutdown(executor_thread_pool_t *self) {
    assert_mutex_lock(&self->mtx);
    self->state = THREAD_POOL_STOPPING;
    pthread_cond_broadcast(&self->cond);
    assert_mutex_unlock(&self->mtx);
}

static executor_vtable_t const executor_thread_pool_vtable = {
    .free = (executor_vtable_free_t) executor_thread_pool_free,
    .submit = (executor_vtable_submit_t) executor_thread_pool_submit,
    .shutdown = (executor_vtable_shutdown_t) executor_thread_pool_shutdown,
};

error_t *executor_thread_pool_new(
    char const *pool_name,
    size_t size,
    executor_thread_pool_t **result
) {
    assert(size > 0);

    error_t *err = NULL;

    executor_thread_pool_t *self = calloc(1, sizeof(executor_thread_pool_t));
    err = error_wrap("Could not allocate memory for the executor", OK_IF(self != NULL));
    if (err) goto calloc_fail;

    self->pool_name = pool_name;
    self->size = size;
    self->threads = vec_pthread_new();

    err = error_wrap("Could not allocate memory for the executor", error_from_common(
        vec_pthread_resize(&self->threads, self->size)));
    if (err) goto resize_fail;

    pthread_mutexattr_t mtx_attr;
    err = error_wrap("Could not initialize mutex attributes", error_from_errno(
        pthread_mutexattr_init(&mtx_attr)));
    if (err) goto mtx_attr_init_fail;

    err = error_wrap("Could initialize a mutex", error_from_errno(
        pthread_mutex_init(&self->mtx, &mtx_attr)));
    pthread_mutexattr_destroy(&mtx_attr);
    if (err) goto mtx_init_fail;

    err = error_wrap("Could initialize a condition variable", error_from_errno(
        pthread_cond_init(&self->cond, NULL)));
    if (err) goto cond_init_fail;

    self->state = THREAD_POOL_STARTING;
    self->tasks = dlist_task_new();
    self->on_error = NULL;

    executor_init(&self->executor, &executor_thread_pool_vtable);

    for (size_t i = 0; i < self->size; ++i) {
        pthread_t thread_id;
        err = error_wrap("Could not spawn a worker thread", error_from_errno(
            pthread_create(&thread_id, NULL, worker_thread, self)));

        // since we have already resized the vector, no allocation, and hence failure, can occur
        error_assert(error_from_common(
            vec_pthread_push(&self->threads, thread_id)));

        if (err) break;
    }

    assert_mutex_lock(&self->mtx);

    if (err) {
        self->state = THREAD_POOL_STOPPING;
    } else {
        self->state = THREAD_POOL_RUNNING;
    }

    pthread_cond_broadcast(&self->cond);

    assert_mutex_unlock(&self->mtx);

    if (!err) {
        *result = self;

        return err;
    }

    // everything below is a failure scenario

    executor_free(&self->executor);

    return err;

cond_init_fail:
    pthread_mutex_destroy(&self->mtx);

mtx_init_fail:
mtx_attr_init_fail:
resize_fail:
    vec_pthread_free(&self->threads);

    free(self);

calloc_fail:
    return err;
}

void executor_thread_pool_on_error(
    executor_thread_pool_t *self,
    executor_thread_pool_on_error_cb_t on_error
) {
    assert_mutex_lock(&self->mtx);
    self->on_error = on_error;
    assert_mutex_unlock(&self->mtx);
}
