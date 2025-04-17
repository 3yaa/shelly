#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
/* got Implicit declaration of function ‘wait’... got answer from 
https://stackoverflow.com/questions/41884685/implicit-declaration-of-function-wait*/
#include <sys/types.h>
#include <sys/wait.h>
#include "argvList.h"

//
#define CMDLINE_MAX 512
#define NON_NULL_MAX 16
#define TOKEN_MAX 32
// #define PIPE_MAX 3
//
#define EXIT_STATUS -1
#define PWD_STATUS 2
#define CD_STATUS 3
//
#define CMDLINE_ERR -4
#define MAX_ARG_ERR -6
#define MAX_ARG_SIZE_ERR -7

struct Command {
    char cmd[CMDLINE_MAX];
    char *argv[NON_NULL_MAX];
    //
    int argvNum;
    ArgvList argvList;
    ArgvNode *currentArgv;
    //
    int numChild;
    pid_t pidChilds[100];
    int statusList[100];
};

// void allocateMemStatus(int *arr, int argvNum) {
//     arr = (int*)calloc(argvNum, sizeof(argvNum));
//     if (arr == NULL) {
//         perror("status type shi");
//     }
// }

// void freeMemStatus(int *arr, int argvNum) {
//     if (arr != NULL) {
//         free(arr);
//         arr = NULL;
//     }
// }

int allocateMemArgv(char *argv[], int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        argv[i] = calloc(cols, sizeof(char));
        if (argv[i] == NULL) {
            //failed to allocate mem
            for (int j = 0; j < i; j++) {
                free(argv[j]);
            }
            return -10;
        }
    }
    return 0;
}

void freeMemArgv(char **argv, int rows) {
    for (int i = 0; i < rows; i++) {
        if (argv[i]) {
            free(argv[i]);
            argv[i] = NULL;
        }
    }
}

void fixNullEntries(struct Command *cmd) {
    //reinitialize null data
    for (size_t i = 0; i < NON_NULL_MAX; i++) {
        if (cmd->argv[i] == NULL) {
            cmd->argv[i] = calloc(TOKEN_MAX, sizeof(char));
        }
    }
}

int parseCommand(struct Command *cmd) {
    fixNullEntries(cmd);
    //parses command into tokens
    size_t i = 0, j = 0, k = 0;
    while (i < CMDLINE_MAX && cmd->cmd[i] != '\0') {
        //too many arguments
        if (j >= NON_NULL_MAX-1) return MAX_ARG_ERR;
        //individual token too big
        if (k >= TOKEN_MAX-1) return MAX_ARG_SIZE_ERR;

        //kills "" && ''
        if ((cmd->cmd[i] == '\'' || cmd->cmd[i] == '"') && j != 0) {
            i++; continue;
        }

        //pipes
        if (cmd->cmd[i] == '|') {
            //makes sure theres cmd before pipe
            if (j == 0 && k == 0 && cmd->argvNum < 1) return -1; 
            // if need to end current argv
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++;
            }
            cmd->argv[j] = NULL;
            cmd->argvNum++;
            //pushes to list
            pushArgv(&cmd->argvList, cmd->argv);
            //
            fixNullEntries(cmd);
            j = 0; k = 0; i++;
            continue;
        }

        //whitespace
        if (cmd->cmd[i] == ' ') {
            //ends argv
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }
            //kills multi whitespace
            while (cmd->cmd[i+1] == ' ') i++;
            i++;
            continue;
        }
        
        //puts cmd into argv
        cmd->argv[j][k] = cmd->cmd[i];
        k++; i++; //goes to new token
    }
    //ends line and puts NULL
    if (k > 0) {
        cmd->argv[j][k] = '\0';
        cmd->argv[j+1] = NULL;
        cmd->argvNum++;
    } else if (k == 1) {
        cmd->argv[j] = NULL;
        cmd->argvNum++;
    }
    //puts the argv into the list 
    if (cmd->argvList.count < cmd->argvNum) {
        pushArgv(&cmd->argvList, cmd->argv);

    }
    return 0;
}

//build in cmd that doesn't need any parsing
int builtInCmd(struct Command *cmd) {
    //EXIT
    if (!strcmp(cmd->cmd, "exit")) {
        fprintf(stderr, "Bye...\n");
        fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd); //need exit val
        return EXIT_STATUS;
    }

    //PWD
    if (!strcmp(cmd->cmd, "pwd")) {
        char pwd[1000];
        fprintf(stdout, "%s\n", getcwd(pwd, sizeof(pwd)));
        fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd);
        return PWD_STATUS;
    }
    return 0;
}

//build in cmd that needs the cmd to be parsed first
int builtInCmd2(struct Command *cmd) {
    //CD
    if (!strcmp(cmd->argv[0], "cd")) {
        chdir(cmd->argv[1]); //changes the directory
        fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd);
        return CD_STATUS;
    }
    return 0;
}

void forkNExec(struct Command *cmd, bool readingPrev, int prevPipeReadEnd) {
    //base case
    if (cmd->currentArgv == NULL) return;

    //set up
    int pipe_fd[2]; bool pipeCreated = false;

    ///basically checks if it has a next argument (pipe) to write to
    bool writingNext = cmd->currentArgv->next != NULL;

    //create a pipe when writing to next pipe
    if (writingNext){ 
        if (pipe(pipe_fd) < 0) { //pipe creation fail
            perror("pipe failed"); //idk can change the message
            return;
        }
        pipeCreated = true;
    }

    //
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        if (pipeCreated) {
            close(pipe_fd[0]);
            close(pipe_fd[1]);
        }
        return;
    }

    if (pid == 0){ //child
        //read from past pipe
        if (readingPrev) {
            dup2(prevPipeReadEnd, STDIN_FILENO); //hook up stdin to prev_pipe_read
            close(prevPipeReadEnd); //close prev_pipe_read
        }
        //write to new pipe
        if (writingNext) {
            close(pipe_fd[0]); //close pipe_read
            dup2(pipe_fd[1], STDOUT_FILENO); //hook up pipe_write to stdout
            close(pipe_fd[1]); //close pipe_write
        }
        execvp(cmd->currentArgv->argv[0], cmd->currentArgv->argv);
        fprintf(stderr, "Error: command not found\n");
        exit(1);
    }

                 //parent

    cmd->pidChilds[cmd->numChild++] = pid;

    //close all created pipe(s)
    if (readingPrev) close(prevPipeReadEnd);
    if (pipeCreated) close(pipe_fd[1]);
    
    //recursive part for the net command
    if (writingNext) {
        cmd->currentArgv = cmd->currentArgv->next;
        forkNExec(cmd, true, pipe_fd[0]);
    }
}

void waitForForks(struct Command *cmd) {
    if (cmd->currentArgv->next == NULL) {
        //get the current child's status
        for (int i = 0; i < cmd->numChild; i++) {
            int status;
            waitpid(cmd->pidChilds[i], &status, 0);
            cmd->statusList[i] = WEXITSTATUS(status);
        }
        //print completion code
        fprintf(stderr, "+ completed '%s' ", cmd->cmd);
        for(int i = 0; i < cmd->argvNum; i++){
            fprintf(stderr, "[%d]", cmd->statusList[i]);
        }
        fprintf(stderr, "\n");
    }
}

int main(void) {
    struct Command command = {0};
    char *eof;
    initializeList(&command.argvList);
    allocateMemArgv(command.argv, NON_NULL_MAX, TOKEN_MAX);

    while (1) {
        /* Print prompt */
        printf("sshell@ucd$ ");
        fflush(stdout);
        
        /* Get command line */
        eof = fgets(command.cmd, CMDLINE_MAX, stdin);
        if (!eof)
        /* Make EOF equate to exit */
        strncpy(command.cmd, "exit\n", CMDLINE_MAX);
        
        /* Print command line if stdin is not provided by terminal */
        if (!isatty(STDIN_FILENO)) {
            printf("%s", command.cmd);
            fflush(stdout);
        }
        
        /* Remove trailing newline from command line */
        char *nl;
        nl = strchr(command.cmd, '\n');
        if (nl)
                *nl = '\0';

        // check for if user just pressed enter (no cmd)
        if(strlen(command.cmd) == 0){
            continue;
        }

        // check if user just put a bunch of whitespace
        int is_whitespace = 1;
        for (int i = 0; command.cmd[i] != '\0'; i++) { 
            // if not whitespace (is actual cmd)
            if (!isspace(command.cmd[i])) {
                is_whitespace = 0; 
                break;
            }
        }

        if (is_whitespace) {
            continue; // prompt for input again
        }

        /* Builtin command */
        int builtCmdResult = builtInCmd(&command);
        if (builtCmdResult == EXIT_STATUS) break;
        if (builtCmdResult == PWD_STATUS) continue;


        //parse input
        command.argvNum = 0;
        int parseResult = parseCommand(&command);
        if(parseResult == MAX_ARG_SIZE_ERR){
            fprintf(stderr, "Error: Argument too long\n");
        }
        else if (parseResult == MAX_ARG_ERR){
            fprintf(stderr, "Error: Too many arguments\n");
        }

        /* Builtin command -> requires parsing */ 
        int builtCmdResult2 = builtInCmd2(&command);
        if (builtCmdResult2 == CD_STATUS) continue;

        //sets up list reading && index
        command.numChild = 0;
        command.currentArgv = command.argvList.head;

        //syscall
        forkNExec(&command, 0, -1);
        waitForForks(&command);

        //resets the list after each command
        freeArgvList(&command.argvList);
    }
    freeMemArgv(command.argv, NON_NULL_MAX);
    return EXIT_SUCCESS;
}