#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <zconf.h>
#include <malloc.h>
#include <assert.h>

#include "strings.h"
#include "thread_safe_list.h"
#include "linked_list.h"

#define SUCCESS (0)
#define LEN_MSEC (400000)
#define ERR_PRINT_CODE(name, code) fprintf(stderr, "error in %s(): %s\n", name, strerror(code))
#define ERR_PRINT(name) fprintf(stderr, "error in %s()\n", name)
#define MAX_STR_COUNT (10000)
#define MAX_WAIT_TIME (20)

static ts_list_t *lines_list;

static int pipe_fds[2];

static void *delayed_append(void *arg) {
    if (arg == NULL) {
        return NULL;
    }
    int pipe_rfd = pipe_fds[0];
    fd_set read_set;
    FD_SET(pipe_rfd, &read_set);
    struct timeval timeout = {
            .tv_sec = MAX_WAIT_TIME,
            .tv_usec = 0
    };
    int res = select(pipe_rfd + 1, &read_set, NULL, NULL, &timeout);
    if (res < 0) {
        ERR_PRINT("select");
        pthread_exit(NULL);
    } else if (res == 0) {
        ERR_PRINT("timeout");
        pthread_exit(NULL);
    }
    char *str = (char *) arg;
    size_t len = strlen(str);
    usleep(len * LEN_MSEC);
    bool appended = append_ts(lines_list, str);
    if (!appended) {
        ERR_PRINT("append_ts");
    }
    pthread_exit(NULL);
}

static void print_line(FILE *fp, void *value) {
    assert(fp);
    assert(value);
    fprintf(fp, "%s ", (char *) value);
}

static void join_thread(void *tidp) {
    pthread_t tid = (pthread_t) tidp;
    int return_code = pthread_join(tid, NULL);
    if (return_code != SUCCESS) {
        ERR_PRINT_CODE("pthread_join", return_code);
    }
}

int main() {
    int return_code = pipe(pipe_fds);
    if (return_code != SUCCESS) {
        ERR_PRINT("pipe");
        pthread_exit(NULL);
    }
    lines_list = init_ts_list();
    if (lines_list == NULL) {
        ERR_PRINT("init_ts_list");
        pthread_exit(NULL);
    }
    list_t *tids_list = init_list();
    if (tids_list == NULL) {
        ERR_PRINT("init_list");
        pthread_exit(NULL);
    }
    char *str_array[MAX_STR_COUNT];
    int lines_count = 0;
    while (true) {
        char *str = fread_str(stdin);
        if (str == NULL) {
            break;
        }
        if (lines_count >= MAX_STR_COUNT) {
            free(str);
            printf("limit is %d lines\n", MAX_STR_COUNT);
            break;
        }
        str_array[lines_count++] = str;
    }
    for (int i = 0; i < lines_count; i++) {
        char *str = str_array[i];
        pthread_t thread_id;
        return_code = pthread_create(&thread_id, NULL, delayed_append, str);
        if (return_code != SUCCESS) {
            ERR_PRINT_CODE("pthread_create", return_code);
            free(str);
            goto FREE_AND_EXIT;
        }
        bool appended = append(tids_list, (void *) thread_id);
        if (!appended) {
            ERR_PRINT("append");
            goto FREE_AND_EXIT;
        }
    }
    ssize_t written = write(pipe_fds[1], "start!", 6);
    if (written == -1) {
        ERR_PRINT("write");
        goto FREE_AND_EXIT;
    }
    iter(tids_list, join_thread);
    print_ts_list(stdout, lines_list, print_line);
    FREE_AND_EXIT:
    {
	      close(pipe_fds[1]);
	      close(pipe_fds[0]);
        free_ts_list(lines_list, free);
        free_list(tids_list, NULL);
        pthread_exit(NULL);
    }
}
