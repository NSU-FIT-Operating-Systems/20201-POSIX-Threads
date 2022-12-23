#include "quasi.h"

#include <assert.h>
#include <string.h>

err_t quasi_new(size_t capacity, quasi_t *result) {
    err_t error = OK;

    string_t *storage = calloc(capacity, sizeof(string_t));
    error = ERR((bool)(storage != NULL), "could not allocate memory for a queue");
    if (ERR_FAILED(error)) goto calloc_fail;

    *result = (quasi_t) {
        .storage = storage,
        .wr_idx = 0,
        .rd_idx = 0,
        .capacity = capacity,
        .full = false,
        .state = QUASI_STATE_OK,
    };

    pthread_mutexattr_t mtx_attr;
    error = ERR((err_errno_t) pthread_mutexattr_init(&mtx_attr),
        "could not initialize mutex attributes");
    if (ERR_FAILED(error)) goto mtxattr_init_fail;

    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    error = ERR((err_errno_t) pthread_mutex_init(&result->mtx, &mtx_attr),
        "could not initialize a mutex");
    pthread_mutexattr_destroy(&mtx_attr);
    if (ERR_FAILED(error)) goto mutex_init_fail;

    error = ERR((err_errno_t) pthread_cond_init(&result->rd_cond, NULL),
        "could not initialize a condition variable");
    if (ERR_FAILED(error)) goto rd_cond_init_fail;

    error = ERR((err_errno_t) pthread_cond_init(&result->wr_cond, NULL),
        "could not initialize a condition variable");
    if (ERR_FAILED(error)) goto wr_cond_init_fail;

    error = ERR((err_errno_t) pthread_cond_init(&result->drop_cond, NULL),
        "could not initialize a condition variable");
    if (ERR_FAILED(error)) goto drop_cond_init_fail;

    return error;

drop_cond_init_fail:
    pthread_cond_destroy(&result->wr_cond);

wr_cond_init_fail:
    pthread_cond_destroy(&result->rd_cond);

rd_cond_init_fail:
    pthread_mutex_destroy(&result->mtx);

mutex_init_fail:
mtxattr_init_fail:
    free(storage);

calloc_fail:
    return error;
}

static bool quasi_is_empty(quasi_t const *self) {
    return self->rd_idx == self->wr_idx && !self->full;
}

static bool quasi_is_full(quasi_t const *self) {
    bool result = self->full;
    assert(!result || self->rd_idx == self->wr_idx);

    return result;
}

static bool quasi_advance_rd(quasi_t *self) {
    if (quasi_is_empty(self)) {
        return false;
    }

    ++self->rd_idx;
    self->rd_idx %= self->capacity;
    self->full = false;

    return true;
}

static bool quasi_advance_wr(quasi_t *self) {
    if (quasi_is_full(self)) {
        return false;
    }

    ++self->wr_idx;
    self->wr_idx %= self->capacity;
    self->full = self->wr_idx == self->rd_idx;

    return true;
}

static bool quasi_pop_unsync(quasi_t *self, string_t *result) {
    size_t rd_idx = self->rd_idx;

    if (!quasi_advance_rd(self)) {
        return false;
    }

    memcpy(result, &self->storage[rd_idx], sizeof(string_t));

    return true;
}

static bool quasi_push_unsync(quasi_t *self, string_t const *message) {
    size_t wr_idx = self->wr_idx;

    if (!quasi_advance_wr(self)) {
        return false;
    }

    memcpy(&self->storage[wr_idx], message, sizeof(string_t));

    return true;
}

static void quasi_lock_clean(quasi_t *self, bool increase_blocked) {
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&self->mtx),
        "could not lock the queue mutex"));

    while (self->state == QUASI_STATE_DROPPED) {
        ERR_ASSERT(ERR((err_errno_t) pthread_cond_wait(&self->drop_cond, &self->mtx),
            "could not wait on drop_cond"));
    }

    assert(self->state == QUASI_STATE_OK);

    if (increase_blocked) {
        ++self->blocked_count;
    }
}

static void quasi_unlock(quasi_t *self, bool dec_blocked) {
    if (dec_blocked) {
        assert(self->blocked_count > 0);

        --self->blocked_count;
    }

    if (self->state == QUASI_STATE_DROPPED && self->blocked_count == 0) {
        self->state = QUASI_STATE_OK;
        pthread_cond_broadcast(&self->drop_cond);
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&self->mtx),
        "could not unlock the queue mutex"));
}

void quasi_free(quasi_t *self) {
    pthread_cond_destroy(&self->drop_cond);
    pthread_cond_destroy(&self->wr_cond);
    pthread_cond_destroy(&self->rd_cond);
    pthread_mutex_destroy(&self->mtx);

    string_t str;

    while (quasi_pop_unsync(self, &str)) {
        string_free(&str);
    }

    free(self->storage);
}

void quasi_drop(quasi_t *self) {
    quasi_lock_clean(self, false);

    string_t str;

    while (quasi_pop_unsync(self, &str)) {
        string_free(&str);
    }

    self->state = QUASI_STATE_DROPPED;
    self->wr_idx = 0;
    self->rd_idx = 0;

    pthread_cond_broadcast(&self->rd_cond);
    pthread_cond_broadcast(&self->wr_cond);

    quasi_unlock(self, false);
}

quasi_state_t quasi_push(quasi_t *self, string_t message) {
    quasi_lock_clean(self, true);

    while (self->state == QUASI_STATE_OK && quasi_is_full(self)) {
        ERR_ASSERT(ERR((err_errno_t) pthread_cond_wait(&self->wr_cond, &self->mtx),
            "could not wait on wr_cond"));
    }

    quasi_state_t result = self->state;

    if (self->state == QUASI_STATE_OK) {
        bool was_empty = quasi_is_empty(self);
        bool pushed = quasi_push_unsync(self, &message);
        assert(pushed);

        if (was_empty) {
            pthread_cond_signal(&self->rd_cond);
        }
    }

    quasi_unlock(self, true);

    return result;
}

quasi_state_t quasi_pop(quasi_t *self, string_t *result) {
    quasi_lock_clean(self, true);

    while (self->state == QUASI_STATE_OK && quasi_is_empty(self)) {
        ERR_ASSERT(ERR((err_errno_t) pthread_cond_wait(&self->rd_cond, &self->mtx),
            "could not wait on rd_cond"));
    }

    quasi_state_t ret = self->state;

    if (self->state == QUASI_STATE_OK) {
        bool was_full = quasi_is_full(self);
        bool popped = quasi_pop_unsync(self, result);
        assert(popped);

        if (was_full) {
            pthread_cond_signal(&self->wr_cond);
        }
    }

    quasi_unlock(self, true);

    return ret;
}
