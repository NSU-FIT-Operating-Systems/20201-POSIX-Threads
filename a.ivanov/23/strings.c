#include "strings.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *fread_str(FILE *fp) {
    assert(fp);
    int ch = '\0';
    // skipping all spaces
    while (isspace(ch = fgetc(fp)));
    if (ch == EOF) {
        return NULL;
    }
    size_t size = 1;
    char *word = (char*) calloc(size, sizeof(*word));
    size_t len = 1;
    while (!isspace(ch) && ch != EOF) {
        word[len - 1] = (char) ch;
        len++;
        if (len >= size) {
            size *= 2;
            char *temp = realloc(word, size);
            if (temp == NULL) {
                free(word);
                return NULL;
            } else {
                word = temp;
            }
        }
        ch = fgetc(fp);
    }
    word[len - 1] = '\0';
    return word;
}

char *freadln_str(FILE *fp) {
    assert(fp);
    int ch = '\0';
    // skipping all spaces
    while (isspace(ch = fgetc(fp)));
    if (ch == EOF || ch == '\n') {
        return NULL;
    }
    size_t size = 1;
    char *word = (char*) calloc(size, sizeof(*word));
    size_t len = 1;
    while (!isspace(ch) && ch != EOF) {
        word[len - 1] = (char) ch;
        len++;
        if (len >= size) {
            size *= 2;
            char *temp = realloc(word, size);
            if (temp == NULL) {
                free(word);
                return NULL;
            } else {
                word = temp;
            }
        }
        ch = fgetc(fp);
    }
    word[len - 1] = '\0';
    return word;
}
