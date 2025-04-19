#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
/* got Implicit declaration of function ‘wait’... got answer from 
https://stackoverflow.com/questions/41884685/implicit-declaration-of-function-wait*/
#include <sys/types.h>
#include <sys/wait.h>
#include "argvList.h"

//
#define CMDLINE_MAX 512
#define NON_NULL_MAX 16
#define TOKEN_MAX 32
//
#define EXIT_STATUS -1
#define PWD_STATUS -2
#define CD_STATUS -3
//
#define FILE_OPEN_ERR -4
#define CMDLINE_ERR -5
#define MAX_ARG_ERR -6
#define OUTPUT_RDIR_ERR -7
#define MISSING_CMD_ERR -8
#define MAX_ARG_SIZE_ERR -9
#define OUPUT_RDIR_LOCATION_ERR -10

struct Command {
    char cmd[CMDLINE_MAX];
    char **argv;
    //
    int argvNum;
    ArgvList argvList;
    ArgvNode *currentArgv;
    //
    pid_t pidChilds[100];
    int statusList[100];
    //
    int backgroundArgv[10]; //holds the argument# of background jobs
};

char** allocateMemArgv(int rows, int cols) {
    char **arr = (char**)malloc(rows*sizeof(char*));
    if (arr == NULL) return NULL;

    for (int i = 0; i < rows; i++) {
        arr[i] = (char*)malloc(cols*sizeof(char));
        if (arr[i] == NULL) {
            for (int j = 0; j < i; j++) free(arr[j]);
            free(arr);
            return NULL;
        }
    }
    return arr;
}

void freeMemArgv(char **arr, int rows) {
    if (arr == NULL) return;

    for (int i = 0; i < rows; i++) {
        free(arr[i]);
    }
    free(arr);
}

void fixNullEntries(struct Command *cmd) {
    //reinitialize null data
    for (size_t i = 0; i < NON_NULL_MAX; i++) {
        if (cmd->argv[i] == NULL) {
            cmd->argv[i] = calloc(TOKEN_MAX, sizeof(char));
        }
    }
}

int outRedirect(char *file) {
    int fd = open(file, O_RDWR, 0644);
    if (fd == -1) {
        // return -1;
    }
    //links it to stdout
    dup2(fd, STDOUT_FILENO);
    close(fd);
    return 0;
}

int missingCMDParse(struct Command *cmd, size_t *i) {
    if (cmd->cmd[*i+1] == '\0') {
        return -1;
    } else if (cmd->cmd[*i+1] == ' ') {
        (*i)++; // at ' '
        while (cmd->cmd[*i+1] == ' ') (*i)++;
        (*i)++;
        if (cmd->cmd[*i] == '\0') return -1;
    }
    return 0;
}

int parseCommand(struct Command *cmd) {
    fixNullEntries(cmd);
    //parses command into tokens
    size_t i = 0, j = 0, k = 0; bool outRedirecting = false; size_t b = 0;
    while (i < CMDLINE_MAX && cmd->cmd[i] != '\0') {
        //too many arguments
        if (j >= NON_NULL_MAX-1) return MAX_ARG_ERR;
        //individual token too big
        if (k >= TOKEN_MAX-1) return MAX_ARG_SIZE_ERR;

        //kills "" && ''
        if ((cmd->cmd[i] == '\'' || cmd->cmd[i] == '"') && j != 0) {
            i++; continue;
        }

        //!checks for background jobs
        if (cmd->cmd[i] == '&') {
            while (cmd->backgroundArgv[b] == 0) {
                cmd->backgroundArgv[b] = cmd->argvNum;
                b++;
                break;
            }
        }

        //output redirection
        if (cmd->cmd[i] == '>') {
            //makes sure theres cmd before output redir
            if (j == 0 && k == 0 && cmd->argvNum < 1) return MISSING_CMD_ERR;
            //makes sure theres output file
            if (missingCMDParse(cmd, &i) == -1) return OUTPUT_RDIR_ERR;
            outRedirecting = true;
            i++; continue;
        }

        //pipes
        if (cmd->cmd[i] == '|') {
            //makes sure theres cmd before pipe
            if (j == 0 && k == 0 && cmd->argvNum < 1) return MISSING_CMD_ERR; 
            //makes sure theres cmd after pipe
            if (missingCMDParse(cmd, &i) == -1) return MISSING_CMD_ERR;
            //cannot pipe after ouput
            if (outRedirecting) return OUPUT_RDIR_LOCATION_ERR;
            
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }
            cmd->argv[j] = NULL;
            cmd->argvNum++;
            //pushes to list
            pushArgv(&cmd->argvList, cmd->argv);
            //resets
            fixNullEntries(cmd);
            j = 0; k = 0; i++;
            continue;
        }

        //whitespace
        if (cmd->cmd[i] == ' ') {
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
                //if out redirecting
                if (outRedirecting) {
                    j = j-1; //goes back to the file word
                    int oRedirSuccess = outRedirect(cmd->argv[j]);
                    if (oRedirSuccess == -1) return FILE_OPEN_ERR;
                }
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

    //check for redirect
    if (outRedirecting) {
        outRedirecting = false;
        int oRedirSuccess = outRedirect(cmd->argv[j]);
        if (oRedirSuccess == -1) return FILE_OPEN_ERR;
        j = j-1; //goes back so it gets nullified
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

//build in cmd
int builtInCmd(struct Command *cmd) {
    ArgvNode* cur = cmd->argvList.head;

    while (cur) {
        //EXIT
        if (!strcmp(cur->argv[0], "exit")) {
            fprintf(stderr, "Bye...\n");
            fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd);
            return EXIT_STATUS;
        }
        //PWD
        if (!strcmp(cur->argv[0], "pwd")) {
            char pwd[100];
            fprintf(stdout, "%s\n", getcwd(pwd, sizeof(pwd)));
            fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd);
            return PWD_STATUS;
        }
        //CD
        if (!strcmp(cur->argv[0], "cd")) {
            int success = chdir(cmd->argv[1]); //changes the directory
            if (success == -1) {
                fprintf(stderr, "Error: cannot cd into directory\n");
                fprintf(stderr, "+ completed '%s' [1]\n", cmd->cmd);
            } else {
                fprintf(stderr, "+ completed '%s' [0]\n", cmd->cmd);
            }
            return CD_STATUS;
        }
        //
        cur = cur->next;
    }
    return 0;
}

void forkNExec(struct Command *cmd, bool readingPrev, int prevPipeReadEnd, int childNum) {
    //base case
    if (childNum >= cmd->argvNum) return;

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

    //fork
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        if (pipeCreated) {
            close(pipe_fd[0]);
            close(pipe_fd[1]);
        }
        return;
    }

    //child
    if (pid == 0){
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

    //!restores default terminal fd
    dup2(STDIN_FILENO, 0); 
    dup2(STDOUT_FILENO, 0);
    
    //stores pid of each child
    cmd->pidChilds[childNum++] = pid;

    //close all created pipe(s)
    if (readingPrev) close(prevPipeReadEnd);
    if (pipeCreated) close(pipe_fd[1]);
    
    //recursive part for the net command
    if (writingNext) {
        cmd->currentArgv = cmd->currentArgv->next;
        forkNExec(cmd, true, pipe_fd[0], childNum);
    }
}

void waitForForks(struct Command *cmd) {
    //get the current child's status
    for (int i = 0; i < cmd->argvNum; i++) {
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

int main(void) {
    char *eof;
    struct Command command = {0};
    initializeList(&command.argvList);
    command.argv = allocateMemArgv(NON_NULL_MAX, TOKEN_MAX);

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
        char *nl = strchr(command.cmd, '\n');
        if (nl) *nl = '\0';

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

        //parse input
        command.argvNum = 0; 
        int parseResult = parseCommand(&command);
        command.currentArgv = command.argvList.head; //sets up list reading && index

        for (int i = 0; i < 10; i++) {
            printf("-------%d\n", command.backgroundArgv[i]);
        }
        
        //command error handling
        if(parseResult == MAX_ARG_SIZE_ERR){
            fprintf(stderr, "Error: Argument too long\n");
            return MAX_ARG_SIZE_ERR;
        } else if (parseResult == MAX_ARG_ERR){
            fprintf(stderr, "Error: Too many arguments\n");
            return MAX_ARG_ERR;
        } else if (parseResult == MISSING_CMD_ERR || parseResult == FILE_OPEN_ERR ||
                   parseResult == OUPUT_RDIR_LOCATION_ERR ||
                   parseResult == OUTPUT_RDIR_ERR) {
            if (parseResult == MISSING_CMD_ERR) {
                fprintf(stderr, "Error: missing command\n");
            } else if (parseResult == OUTPUT_RDIR_ERR) {
                fprintf(stderr, "Error: no output file\n");
            } else if (parseResult == FILE_OPEN_ERR) {
                fprintf(stderr, "Error: cannot open output file\n");
            } else if (parseResult == OUPUT_RDIR_LOCATION_ERR) {
                fprintf(stderr, "Error: mislocated output redirection\n");
            }

            resetArgvList(&command.argvList);
            continue;
        }

        /* Builtin command -> requires parsing */ 
        int builtCmdResult = builtInCmd(&command);
        if (builtCmdResult == EXIT_STATUS) break;
        if (builtCmdResult == PWD_STATUS || builtCmdResult == CD_STATUS) {
            resetArgvList(&command.argvList);
            continue;
        }

        // ArgvNode *cur = command.argvList.head;
        // while (cur) {
        //     for (int i = 0; cur->argv[i] != NULL; i++) {
        //         printf("%s\n", cur->argv[i]);
        //     }
        //     printf("\n");
        //     cur = cur->next;
        // }

        //syscall
        forkNExec(&command, 0, -1, 0);
        waitForForks(&command);

        //resets the list after each command
        resetArgvList(&command.argvList);
    }
    freeMemArgv(command.argv, NON_NULL_MAX);
    freeArgvList(&command.argvList);
    return EXIT_SUCCESS;
}