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

        //get each command from the entire command
        // ArgvNode *cur = command.argvList.head;
        // while (cur) {
        //     printf("%s\n", cur->argv[0]);
        //     cur = cur->next;
        // }

        // /* TEMP PRINT OF LINKED LIST*/
        // printf("\n\n\n");
        // ArgvNode *curr = command.argvList.head;
        // while (curr) {
        //     for (size_t i = 0; curr->argv[i] != NULL; i++) {
        //         printf("%s ", curr->argv[i]);
        //     }
        //     printf("\n");
        //     curr = curr->next;
        // }

// void forkNExec(struct Command *cmd, size_t i) {
//     if(i >= cmd->argumentsNum) return;

//     // char** args_list[3] = {cmd->arguments, cmd->argument2, cmd->argument3};
//     // char** args = args_list[i];

//     pid_t pid1, pid2, pid3;
//     int pipe1_fd[2];
//     int pipe2_fd[2];

//     //
//     if (cmd->argumentsNum > 1) pipe(pipe1_fd);
//     if (cmd->argumentsNum > 2) pipe(pipe2_fd);

//     // First command
//     pid1 = fork();
//     if (pid1 == 0) {
//         if (cmd->argumentsNum > 1) {
//             close(pipe1_fd[0]); //doesn't need pipe_R
//             dup2(pipe1_fd[1], STDOUT_FILENO); //out with pipe_W
//             close(pipe1_fd[1]); //doesn't need pipe_W
//         }
//         execvp(cmd->arguments[0], cmd->arguments);
//         fprintf(stderr, "Error: command not found\n");
//         exit(1);
//     }

//     // Second command (if exists)
//     if (cmd->argumentsNum >= 2) {
//         pid2 = fork();
//         if (pid2 == 0) {
//             // Set up input from previous pipe -readingPrev
//             close(pipe1_fd[1]); //doesn't need pipe_W
//             dup2(pipe1_fd[0], STDIN_FILENO); //in with pipe_R
//             close(pipe1_fd[0]); //doesn't need pipe_R
            
//             // Set up output to next pipe if needed -writingNext
//             if (cmd->argumentsNum > 2) {
//                 close(pipe2_fd[0]); //doesn't need pipe_R
//                 dup2(pipe2_fd[1], STDOUT_FILENO); //out with pipe_W
//                 close(pipe2_fd[1]); //doesn't need pipe_W
//             }
            
//             execvp(cmd->argument2[0], cmd->argument2);
//             fprintf(stderr, "Error: command not found\n");
//             exit(1);
//         }
//         // Parent closes pipe ends it won't use
//         close(pipe1_fd[0]);
//         close(pipe1_fd[1]);
//     }

//     // Third command (if exists)
//     if (cmd->argumentsNum > 2) {
//         pid3 = fork();
//         if (pid3 == 0) {
//             close(pipe2_fd[1]); //doesn't need pipe_W
//             dup2(pipe2_fd[0], STDIN_FILENO); //in with pipe_R
//             close(pipe2_fd[0]); //doesn't need pipe_R
            
//             execvp(cmd->argument3[0], cmd->argument3);
//             fprintf(stderr, "Error: command not found\n");
//             exit(1);
//         }
//         // Parent closes pipe ends it won't use
//         close(pipe2_fd[0]);
//         close(pipe2_fd[1]);
//     }

//     // Wait for all children to complete
//     int status1, status2, status3;
//     if (cmd->argumentsNum >= 3) {
//         waitpid(pid3, &status3, 0);
//         fprintf(stderr, "+ completed '%s' [%d][%d][%d]\n", cmd->cmd, 
//             WEXITSTATUS(status1), WEXITSTATUS(status2), WEXITSTATUS(status3));
//     } else if (cmd->argumentsNum == 2) {
//         waitpid(pid2, &status2, 0);
//         fprintf(stderr, "+ completed '%s' [%d][%d]\n", cmd->cmd, 
//                 WEXITSTATUS(status1), WEXITSTATUS(status2));
//     } else {
//         waitpid(pid1, &status1, 0);
//         fprintf(stderr, "+ completed '%s' [%d]\n", cmd->cmd, 
//             WEXITSTATUS(status1));
//     }
// }