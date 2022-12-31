#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "copy_info.h"
#include "sleep.h"
#include "task_stack.h"

#define BLOCK_SIZE 4096

enum MAIN_CODES {
    MAIN_OK,
    MAIN_WRONG_ARGS,
    MAIN_MEMORY_ERROR,
};

bool create_dir_if_not_exist(const char *path) {
    // Check if the directory already exists
    struct stat st;
    if (stat(path, &st) == 0) {
        // The directory already exists
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        fprintf(stderr, "Error: %s is not a directory\n", path);
        return false;
    }

    // Create the directory with read, write, and execute permissions for the owner
    if (mkdir(path, 0777) != 0) {
        fprintf(stderr, "Error: Unable to create directory %s\n", path);
        return false;
    }

    return true;
}

// Called in a single thread context
bool check_args(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <src> <dst>\n", argv[0]);
        return false;
    }

    // Check if the first argument is a directory
    struct stat st;
    if (stat(argv[1], &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", argv[1]);
        return false;
    }

    return create_dir_if_not_exist(argv[2]);
}

// Function to create a thread with a minimal stack size and detach it
int create_thread(void *(*thread_func)(void *), void *arg) {
    pthread_t thread;
    pthread_attr_t attr;

    // Initialize the thread attributes
    pthread_attr_init(&attr);

    // Set the stack size to the minimum acceptable value
    pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);

    // Create the thread
    int ret = pthread_create(&thread, &attr, thread_func, arg);
    if (ret != 0) {
        if (ret != EAGAIN) {
            fprintf(stderr, "Error creating thread: %s\n", strerror(ret));
        }
        return ret;
    }

    // Thread is detached to not lose any memory when 
    pthread_detach(thread);

    return 0;
}

void traverse_dir(struct stack *stack, struct copy_info *info) {
    assert(stack != NULL);
    assert(info != NULL);

    // Open the source directory
    DIR *dir = opendir(info->src_path);
    while (dir == NULL && errno == EMFILE) {
        my_sleep();
        dir = opendir(info->src_path);
    }
    if (dir == NULL) {
        fprintf(stderr, "Error: Unable to open %s\n", info->src_path);
        goto FREE_INFO;
    }
    if (!create_dir_if_not_exist(info->dst_path)) {
        goto CLOSE_DIR;
    }

    // Traverse the directory and create a new copy_info struct for each file
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Ignore the "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (entry->d_type == DT_DIR || entry->d_type == DT_REG) {
            // Allocate memory for the new copy_info struct
            struct copy_info *new_info = create_copy_info(info, entry->d_name);
            
            if (new_info == NULL) {
                fprintf(stderr, "Error: failed to allocate copy_info\n");
                continue;
            }
            
            if (entry->d_type == DT_DIR) {
                new_info->is_dir = true;
            } else {
                new_info->is_dir = false;
            }
            if (!stack_push(stack, new_info)) {
                fprintf(stderr, "Error: failed to push into stack\n");
                goto FREE_DST_PATH;
            }

            continue;

            // Memory will be freed in case of error
            FREE_DST_PATH:
            free(new_info->dst_path);
            free(new_info->src_path);
            free(new_info);
        }
    }


    CLOSE_DIR:
    closedir(dir);

    FREE_INFO:
    free(info->src_path);
    free(info->dst_path);
    free(info);

    stack_unregister_dir(stack);
}

void copy_file(struct copy_info *info) {
    assert(!info->is_dir);

    // Open the source files
    FILE *src_file = NULL;
    FILE *dst_file = NULL;
    char *err_path = NULL;
    while (src_file == NULL && dst_file == NULL) {
        src_file = fopen(info->src_path, "r");
        if (src_file == NULL) {
            if (errno != EMFILE) {
                err_path = info->src_path;
                break;
            }
            my_sleep();
            continue;
        }
        dst_file = fopen(info->dst_path, "w");
        if (dst_file == NULL) {
            fclose(src_file);
            src_file = NULL;

            if (errno != EMFILE) {
                err_path = info->dst_path;
                break;
            }
            my_sleep();
        }
    }

    if (dst_file == NULL) {
        fprintf(stderr, "Error: Unable to open %s\n", err_path);
        goto FREE_INFO;
    }

    // Allocate a buffer for reading and writing the file
    char *buffer = malloc(BLOCK_SIZE);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        goto CLOSE;
    }

    // Read and write the file in blocks of BLOCK_SIZE bytes
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, src_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst_file) != bytes_read) {
            fprintf(stderr, "Error: Unable to write to %s\n", info->dst_path);
            break;
        }
    }

    // Check for errors while reading the file
    if (ferror(src_file)) {
        fprintf(stderr, "Error: Unable to read from %s\n", info->src_path);
    }

    free(buffer);

    CLOSE:
    fclose(dst_file);
    fclose(src_file);
    FREE_INFO:
    free(info->src_path);
    free(info->dst_path);
    free(info);
}

void *work(void *arg) {
    if (arg == NULL) {
        return NULL;
    }

    struct copy_info *info = (struct copy_info *)arg;

    if (info->is_dir) {
        traverse_dir(info->stack, info);
    } else {
        copy_file(info);
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (!check_args(argc, argv)) {
        return MAIN_WRONG_ARGS;
    }

    struct copy_info *main_info = malloc(sizeof(struct copy_info));
    if (main_info == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        return MAIN_MEMORY_ERROR;
    }

    // Copy the first and second arguments to the src_path and dst_path fields
    main_info->src_path = strdup(argv[1]);
    main_info->dst_path = strdup(argv[2]);
    if (main_info->src_path == NULL || main_info->dst_path == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        goto FREE_INFO;
    }
    main_info->is_dir = true;

    struct stack *stack = (struct stack *)malloc(sizeof(struct stack));
    if (stack == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        goto FREE_INFO;
    }

    init_stack(stack);
    if (!stack_push(stack, main_info)) {
        goto FREE_STACK;
    }

    while (!stack_stopped(stack)) {
        struct copy_info *info = stack_pop(stack);
        if (info == NULL) {
            continue;
        }
        
        if (info->is_dir) {
            stack_register_dir(stack);
        }
        info->stack = stack;

        if (create_thread(work, info) != 0) {
            free(info->src_path);
            free(info->dst_path);
            free(info);

            goto FREE_STACK;
        }
    }

    free_stack(stack);
    free(stack);
    pthread_exit(NULL);

    FREE_STACK:
    free_stack(stack);
    free(stack);
    FREE_INFO:
    free(main_info->src_path);
    free(main_info->dst_path);
    free(main_info);

    pthread_exit(NULL);
}
