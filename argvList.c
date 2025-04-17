#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argvList.h"

void initializeList(ArgvList *list) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

char** copyArgv(char *const argv[]) {
    char **temp = malloc((16)*sizeof(char*));

    size_t i = 0;
    for (; argv[i] != NULL; i++) {
        temp[i] = strdup(argv[i]);
    }
    temp[i+1] = NULL;
    return temp;
}

void pushArgv(ArgvList *list, char *const argv[]) {
    ArgvNode *new = malloc(sizeof(ArgvNode));

    new->argv = copyArgv(argv);
    new->next = NULL;

    if (list->tail) {
        list->tail->next = new;
    } else {
        list->head = new;
    }

    list->tail = new;
    list->count++;
}

void freeArgvList(ArgvList *list) {
    ArgvNode *cur = list->head;

    while (cur) {
        ArgvNode *next = cur->next;

        for (size_t i =0; cur->argv[i] != NULL; i++) {
            free(cur->argv[i]);
        }
        free(cur->argv);
        free(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}