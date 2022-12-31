#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <fcntl.h>
#include "queue.h"
#define BUFSIZE 10000

dirqueue * copyQueue;
pthread_attr_t detachFlag;

//Keeps track of the number of currently open threads to notify the main thread of when to stop waiting on the queue
_Atomic size_t threadsStarted = 1;
_Atomic size_t threadsEnded = 0;

char * appendDir(char * dir, char * name){
    char * res = (char*)calloc(strlen(dir) + strlen(name) + 2, sizeof(char));
    strcpy(res, dir);
    strcat(res, "/");
    strcat(res, name);
    return res;
}

dirpair * makeDirPair(dirpair * path, char * name){
    dirpair * res = (dirpair*)malloc(sizeof(dirpair));
    if(res == NULL){
        return NULL;
    }
    res->src = appendDir(path->src, name);
    res->dest = appendDir(path->dest, name);
    return res;
}

void freeDirPair(dirpair * dir){
    free(dir->src);
    free(dir->dest);
    free(dir);
}

int openWithWait(char * fname, int flags, mode_t mode){
    int fdsrc = open(fname, flags, mode);
    while(fdsrc == -1){
        if((errno == EMFILE) || (errno == ENFILE)){
            usleep(100);
            fdsrc = open(fname, flags, mode);
        }
        else {
            //printf("%d!!!\n", errno);
            return -1;
        }
    }
    return fdsrc;
}

int copyFile(dirpair * dir, mode_t mode){
    int fdsrc = openWithWait(dir->src, O_RDONLY, 0700);
    if(fdsrc == -1){
        printf("Failed to open file! %s\n", dir->src);
        return 1;
    }
    int fddest = openWithWait(dir->dest, O_WRONLY | O_CREAT, 0700);
    if(fddest == -1){
        printf("Failed to open file %s\n", dir->dest);
        close(fdsrc);
        return 1;
    }
    char buf[BUFSIZE];
    size_t bytesread = read(fdsrc, buf, BUFSIZE);
    while(bytesread > 0){
        write(fddest, buf, bytesread);
        bytesread = read(fdsrc, buf, BUFSIZE);
    }
    close(fdsrc);
    close(fddest);
    return 0;
}

dirnode * readAll(DIR * d, dirpair * path){
    dirnode * res = NULL;
    dirnode ** ptr = &res;
    struct dirent * entry = readdir(d);
    while(entry != NULL){
        if((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
            dirnode *new = (dirnode *) malloc(sizeof(dirnode));
            new->dir = makeDirPair(path, entry->d_name);
            struct stat s;
            if(lstat(new->dir->src, &s)){
                printf("Could not call stat on %s %d\n", new->dir->src, errno);
            }
            new->mode = s.st_mode;
            new->next = NULL;
            *ptr = new;
            ptr = &new->next;
        }
        entry = readdir(d);
    }
    return res;
}

void * copyDir(void * arg){
    assert(arg != NULL);

    struct dirPair * dir = (struct dirPair *)arg;

    DIR * currDir = opendir((char*)dir->src);
    if(currDir == NULL){
        if(errno == EACCES){
            printf("Insufficient permissions to read directory %s\n", dir->src);
            ++threadsEnded;
            pthread_exit(NULL);
        }
        //Wait for system-wide open file table to clear up in a sleep cycle.
        else if((errno == ENFILE) || (errno == EMFILE)){
            while((currDir == NULL) && ((errno == ENFILE) || (errno == EMFILE))){
                usleep(10000);
                currDir = opendir((char*)dir->src);
            }
            if(currDir == NULL){
                printf("Directory %s is no longer valid\n", dir->src);
                ++threadsEnded;
                pthread_exit(NULL);
            }
        }
        else{
            printf("Failed to open directory %s %d\n", dir->src, errno);
            ++threadsEnded;
            pthread_exit(NULL);
        }
    }
    dirnode * files = readAll(currDir, dir);
    closedir(currDir);
    freeDirPair(dir);

    while(files != NULL) {
        if (S_ISDIR(files->mode)) {
            if(mkdir(files->dir->dest, files->mode & 07777)){
                if(errno != EEXIST){
                    printf("Could not create directory %s \n", files->dir->dest);
                    ++threadsEnded;
                    pthread_exit(NULL);
                }
            }
            pthread_t id;
            if(pthread_create(&id, &detachFlag, &copyDir, files->dir)) {
                pushQueue(copyQueue, files->dir, files->mode);
            }
            else {
                threadsStarted++;
            }
        } else if (S_ISREG(files->mode)) {;
            if(copyFile(files->dir, files->mode)){
                printf("Could not copy file %s\n", files->dir->src);
            }
            freeDirPair(files->dir);
        }
        dirnode * next = files->next;
        free(files);
        files = next;
    }
    ++threadsEnded;
    pthread_exit(NULL);
}

int main(int argc, char ** argv){
    if(argc < 3){
        printf("Provide two directories!\n");
        pthread_exit(NULL);
    }
    char * src = (char*)calloc(strlen(argv[1]) + 1, sizeof(char));
    strcpy(src, argv[1]);
    char * dest = (char*)calloc(strlen(argv[2]) + 1, sizeof(char));
    strcpy(dest, argv[2]);

    dirpair * root = (dirpair*)malloc(sizeof(dirpair));

    if(root == NULL){
        printf("Could not start copy\n");
        pthread_exit(NULL);
    }
    root->src = src;
    root->dest = dest;

    if(pthread_attr_init(&detachFlag)){
        printf("No memory\n");
        freeDirPair(root);
        pthread_exit(NULL);
    }
    pthread_attr_setdetachstate(&detachFlag, PTHREAD_CREATE_DETACHED);

    if(initQueue(&copyQueue)){
        printf("Could not init queue\n");
        freeDirPair(root);
        pthread_exit(NULL);
    }
    pthread_t id;
    if(pthread_create(&id, NULL, &copyDir, root)) {
        printf("Could not start a new thread\n");
        freeQueue(copyQueue);
        freeDirPair(root);
        pthread_exit(NULL);
    }
    while(threadsStarted > threadsEnded){
        dirpair * task = popQueue(copyQueue);
        if(task != NULL){
            threadsStarted++;
            while(pthread_create(&id, &detachFlag, &copyDir, task)){
                if(errno == EAGAIN) {
                    usleep(1000);
                }
                else {
                    ++threadsEnded;
                    break;
                }
            }
        }
    }
    freeQueue(copyQueue);
    pthread_exit(NULL);
}
