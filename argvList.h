#ifndef ARGV_LIST_H
#define ARGV_LIST_H

typedef struct ArgvNode {
    char **argv;
    struct ArgvNode *next;
} ArgvNode;

typedef struct ArgvList {
    ArgvNode *head;
    ArgvNode *tail;
    int count;
} ArgvList;


void initializeList(ArgvList *list);
char** copyArgv(char *const argv[]);
void pushArgv(ArgvList *list, char *const argv[]);
void freeArgvList(ArgvList *list);

#endif