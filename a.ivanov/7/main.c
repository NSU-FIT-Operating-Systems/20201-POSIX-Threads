#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#define SUCCESS (0)
#define FAIL (-1)
#define ARGC_REQUIRED (2 + 1)
#define USAGE "./prog <src_dir> <dest_dir>"
#define RELOAD_MAX (1000)
#define RELOAD_TIME_SEC (5)

static pthread_attr_t lw_attr;

typedef struct cpy_info_t {
    char *src_path;
    char *dest_path;
} cpy_info_t;

typedef enum cpy_status_t {
    OK, MEM_FAIL, SELF_CPY_FAIL, OPENDIR_FAIL,
    FOPEN_FAIL, FPUTC_FAIL, FCLOSE_FAIL,
    THR_CREATE_FAIL, MKDIR_FAIL,
} cpy_status_t;

static char *cpy_strerror(cpy_status_t status) {
    static char str_status[100];
    char *res;
    switch (status) {
        case OK : res = "Success"; break;
        case MEM_FAIL: res = "Memory error"; break;
        case SELF_CPY_FAIL: res = "Self copy error"; break;
        case OPENDIR_FAIL: res = "opendir() failed"; break;
        case FOPEN_FAIL: res = "fopen() failed"; break;
        case FPUTC_FAIL: res = "fputc() failed"; break;
        case FCLOSE_FAIL: res = "fclose() failed"; break;
        case THR_CREATE_FAIL: res = "pthread_create() failed"; break;
        case MKDIR_FAIL: res = "mkdir() failed"; break;
        default: break;
    }
    strcpy(str_status, res);
    return str_status;
}

static FILE *fopen_blocking(const char *file_name, const char *mode) {
    FILE *file = fopen(file_name, mode);
    for (int i = 0; i < RELOAD_MAX && file == NULL && errno == EMFILE; i++) {
        sleep(RELOAD_TIME_SEC);
        file = fopen(file_name, mode);
    }
    return file;
}

static cpy_status_t fcpy(const char *src_file_name, const char *dest_file_name) {
    assert(src_file_name);
    assert(dest_file_name);
    FILE *src_file = fopen_blocking(src_file_name, "r");
    if (NULL == src_file) {
        return FOPEN_FAIL;
    }
    FILE *dest_file = fopen_blocking(dest_file_name, "w");
    if (NULL == dest_file) {
        fclose(src_file);
        return FOPEN_FAIL;
    }
    cpy_status_t retval = OK;
    int ch;
    while ((ch = fgetc(src_file)) != EOF) {
        int wch = fputc(ch, dest_file);
        if (wch == EOF) {
            fprintf(stderr, "error in fputc()\n");
            retval = FPUTC_FAIL;
            break;
        }
    }
    int code = fclose(src_file);
    if (code == EOF) {
        retval = FCLOSE_FAIL;
    }
    code = fclose(dest_file);
    if (code == EOF) {
        retval = FCLOSE_FAIL;
    }
    return retval;
}

static char *concat_file_names(const char *path_name, const char *dir_name) {
    size_t path_len = strlen(path_name);
    size_t len = path_len + 1 + strlen(dir_name);
    char *full_name = (char *) malloc(len + 1);
    if (NULL == full_name) {
        return full_name;
    }
    strcpy(full_name, path_name);
    if (path_name[path_len - 1] == '/') {
        strcpy(full_name + strlen(path_name), dir_name);
        return full_name;
    }
    full_name[strlen(path_name)] = '/';
    strcpy(full_name + strlen(path_name) + 1, dir_name);
    return full_name;
}

static bool make_dir(const char *dir_path) {
    assert(dir_path);
    struct stat st = {0};
    if (stat(dir_path, &st) != FAIL) {
        return true;
    }
    int return_code = mkdir(dir_path, 0700);
    if (return_code == FAIL) {
        return false;
    }
    return true;
}

static bool is_special_dir(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static size_t get_files_count(DIR *src_dir) {
    size_t counter = 0;
    struct dirent *entry;
    while ((entry = readdir(src_dir)) != NULL) {
        if (is_special_dir(entry->d_name)) {
            continue;
        }
        if (entry->d_type == DT_DIR || entry->d_type == DT_REG) {
            counter++;
        }
    }
    seekdir(src_dir, 0);
    return counter;
}

static void free_cpy_info(cpy_info_t *info) {
    free(info->src_path);
    free(info->dest_path);
    free(info);
}

static void *fcpy_start(void *arg) {
    assert(arg);
    cpy_info_t *names = (cpy_info_t *) arg;
    char *src_file_path = names->src_path;
    char *dest_file_path = names->dest_path;
    cpy_status_t code = fcpy(src_file_path, dest_file_path);
    if (code != OK) {
        fprintf(stderr, "fail in fcpy(): %s\n"
                        "src_file: %s, dest_file: %s\n"
                        "details: %s\n", cpy_strerror(code), src_file_path, dest_file_path, strerror(errno));
    }
    free_cpy_info(names);
    return (void *) code;
}

static bool check_self_copy(const char *src_dir, const char *dest_path) {
    size_t len1 = strlen(src_dir);
    size_t len2 = strlen(dest_path);
    size_t min_len = len1 > len2 ? len2 : len1;
    for (size_t i = 0; i < min_len; i++) {
        if (src_dir[i] != dest_path[i]) {
            return false;
        }
    }
    return true;
}

static DIR *blocking_opendir(const char *dir_name) {
    DIR *dir = opendir(dir_name);
    for (size_t i = 0; i < RELOAD_MAX && dir == NULL && errno == EMFILE; i++) {
        sleep(RELOAD_TIME_SEC);
        dir = opendir(dir_name);
    }
    return dir;
}

static int blocking_pthread_create_detached(pthread_t *new_thread,
                                            const pthread_attr_t *attr,
                                            void *(*start_routine) (void *),
                                            void * arg) {
    int return_code;
    size_t attempts_count = 0;
    do {
        if (attempts_count > 0) {
            sleep(RELOAD_TIME_SEC);
        }
        return_code = pthread_create(new_thread, attr, start_routine, arg);
        attempts_count++;
    } while (return_code == EAGAIN && attempts_count <= RELOAD_MAX);
    return_code = pthread_detach(*new_thread);
    return return_code;
}

static cpy_info_t *cpy_info_append(cpy_info_t *old, const char *new) {
    char *src_file_path = concat_file_names(old->src_path, new);
    if (src_file_path == NULL) {
        return NULL;
    }
    char *dest_file_path = concat_file_names(old->dest_path, new);
    if (dest_file_path == NULL) {
        free(src_file_path);
        return NULL;
    }
    cpy_info_t *cpy_files_info = (cpy_info_t *) malloc(sizeof(*cpy_files_info));
    if (cpy_files_info == NULL) {
        free(src_file_path);
        free(dest_file_path);
        return NULL;
    }
    cpy_files_info->src_path = src_file_path;
    cpy_files_info->dest_path = dest_file_path;
    return cpy_files_info;
}

static void *cp_recursive_parallel(void *arg) {
    cpy_status_t ret_val = OK;
    cpy_info_t *cpy = (cpy_info_t *) arg;
    assert(cpy);
    assert(cpy->src_path);
    assert(cpy->dest_path);
    DIR *src_dir = blocking_opendir(cpy->src_path);
    if (src_dir == NULL) {
        ret_val = OPENDIR_FAIL;
        fprintf(stderr, "error: %s\n", cpy_strerror(ret_val));
        free_cpy_info(cpy);
        return (void *) ret_val;
    }
    size_t files_count = get_files_count(src_dir);
    pthread_t threads[files_count];
    size_t file_idx = 0;
    struct dirent *entry;
    while ((entry = readdir(src_dir)) != NULL) {
        if (is_special_dir(entry->d_name)) {
            continue;
        }
        if (entry->d_type == DT_REG) {
            ret_val = MEM_FAIL;
            cpy_info_t *cpy_files_info = cpy_info_append(cpy, entry->d_name);
            if (cpy_files_info == NULL) {
                goto FREE_MEM_AND_EXIT;
            }
            int return_code = blocking_pthread_create_detached(&threads[file_idx++], &lw_attr,
                                                               fcpy_start, cpy_files_info);
            if (return_code != SUCCESS) {
                ret_val = THR_CREATE_FAIL;
                free_cpy_info(cpy_files_info);
                goto FREE_MEM_AND_EXIT;
            }
        } else if (entry->d_type == DT_DIR) {
            bool self_copy = check_self_copy(entry->d_name, cpy->dest_path);
            if (self_copy) {
                ret_val = SELF_CPY_FAIL;
                goto FREE_MEM_AND_EXIT;
            }
            cpy_info_t *new_cpy = cpy_info_append(cpy, entry->d_name);
            if (new_cpy == NULL) {
                ret_val = MEM_FAIL;
                goto FREE_MEM_AND_EXIT;
            }
            bool dir_created = make_dir(new_cpy->dest_path);
            if (!dir_created) {
                ret_val = MKDIR_FAIL;
                free_cpy_info(new_cpy);
                goto FREE_MEM_AND_EXIT;
            }
            int return_code = blocking_pthread_create_detached(&threads[file_idx++], &lw_attr,
                                                               cp_recursive_parallel,
                                                               new_cpy);
            if (return_code != SUCCESS) {
                ret_val = THR_CREATE_FAIL;
                free_cpy_info(new_cpy);
                goto FREE_MEM_AND_EXIT;
            }
        }
    }
    closedir(src_dir);
    free_cpy_info(cpy);
    ret_val = OK;
    pthread_exit((void *) ret_val);
    FREE_MEM_AND_EXIT:
    {
        fprintf(stderr, "error: %s\n", cpy_strerror(ret_val));
        free_cpy_info(cpy);
        closedir(src_dir);
        pthread_exit((void *) ret_val);
    }
}

static cpy_info_t *parse_args(int argc, char *argv[]) {
    if (argc != ARGC_REQUIRED) {
        return NULL;
    }
    cpy_info_t *args = (cpy_info_t *) malloc(sizeof(*args));
    args->src_path = (char *) malloc(strlen(argv[1]) + 1);
    if (args->src_path == NULL) {
        free(args);
        return NULL;
    }
    args->dest_path = (char *) malloc(strlen(argv[2]) + 1);
    if (args->src_path == NULL) {
        free(args->src_path);
        free(args);
        return NULL;
    }
    strcpy(args->src_path, argv[1]);
    strcpy(args->dest_path, argv[2]);
    return args;
}

int main(int argc, char *argv[]) {
    cpy_info_t *args = parse_args(argc, argv);
    if (args == NULL) {
        fprintf(stderr, "%s\n", USAGE);
        return FAIL;
    }
    int return_code = pthread_attr_init(&lw_attr);
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_init_attr(): %s\n", strerror(return_code));
        return FAIL;
    }
    size_t stack_size = PTHREAD_STACK_MIN;
    return_code = pthread_attr_setstacksize(&lw_attr, stack_size); // 8 388 608
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_attr_setstacksize(): %s\n", strerror(return_code));
        return FAIL;
    }
    pthread_t tid;
    return_code = pthread_create(&tid, &lw_attr, cp_recursive_parallel, args);
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_create(): %s\n", strerror(return_code));
        pthread_exit(NULL);
    }
    return_code = pthread_detach(tid);
    if (return_code != SUCCESS) {
        fprintf(stderr, "pthread_detach() failed: %s\n", strerror(return_code));
    }
    pthread_exit(NULL);
}
