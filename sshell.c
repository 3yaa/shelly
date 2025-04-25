
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
//
#include "argvList.h"

//
#define TOKEN_MAX 32
#define CMDLINE_MAX 512
#define NON_NULL_MAX 16
//
#define EXIT_FAIL -1
#define CD_STATUS -2
#define PWD_STATUS -3
#define EXIT_STATUS -4
//
#define CMDLINE_ERR -5
#define MAX_ARG_ERR -6
#define INPUT_RDIR_ERR -7 
#define OUTPUT_RDIR_ERR -8
#define MISSING_CMD_ERR -9
#define MAX_ARG_SIZE_ERR -10
#define INPUT_LOCATION_ERR -11 
#define OUTPUT_LOCATION_ERR -12
#define INPUT_FILE_OPEN_ERR -13 
#define OUTPUT_FILE_OPEN_ERR -14
#define BACK_JOB_LOCATION_ERR -15


typedef struct BackCmd {
    bool isActive; //false -> reuse
    int *backChildStatus; //stores each child's status
    pid_t *backChildPids; //stores backJob's PID since rest is waiting on that
    char oldCmd[CMDLINE_MAX]; //stores the CMD of the background job
} BackCmd;

typedef struct Command {
    int argvNum; 
    char **argv; //[16][32]
    bool isBackJob;
    int terminalFd[2]; //holds location of terminal [in,out]
    ArgvList argvList;
    pid_t *pidChilds; //stores PID //[16]
    ArgvNode *currentArgv;
    char cmd[CMDLINE_MAX]; //input cmd
    //background jobs
    int backCmdCount; 
    BackCmd *backCmd;
    //redirection
    char *inputFile; //stores the latest input file
    char *outputFile; //stores the latest output file
} Command;


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

void allocateChildPid(struct Command *cmd) {
    cmd->pidChilds = malloc(NON_NULL_MAX*sizeof(pid_t));
    if (cmd->pidChilds == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
}

void freeChildPid(struct Command *cmd) {
    if (cmd->pidChilds != NULL) {
        free(cmd->pidChilds);
        cmd->pidChilds = NULL;
    }
}

void allocateBackMembers(struct BackCmd *backCmdEntry, int childNum) {
    backCmdEntry->backChildPids = malloc(childNum*sizeof(pid_t));
    backCmdEntry->backChildStatus = malloc(childNum*sizeof(int));
    if (backCmdEntry->backChildPids == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    if (backCmdEntry->backChildStatus == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
}

void allocateBackCmd(struct Command *cmd) {
    //initial allocation
    if (cmd->backCmd == NULL) {
        cmd->backCmd = malloc(NON_NULL_MAX*sizeof(BackCmd));
        if (cmd->backCmd == NULL) {
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }
        cmd->backCmdCount = NON_NULL_MAX;
        //initializes
        for (int i = 0; i < cmd->backCmdCount; i++) {
            cmd->backCmd[i].isActive = false;
            cmd->backCmd[i].oldCmd[0] = '\0';
            allocateBackMembers(&cmd->backCmd[i], cmd->argvNum);
        }
    } else {
        //resize
        int newCount = cmd->backCmdCount+NON_NULL_MAX;
        BackCmd *temp = realloc(cmd->backCmd, newCount * sizeof(BackCmd));
        if (temp == NULL) {
            perror("realloc failed");
            free(cmd->backCmd);
            exit(EXIT_FAILURE);
        }
        cmd->backCmd = temp;
        //initializes
        for (int i = cmd->backCmdCount; i < newCount; i++) {
            cmd->backCmd[i].isActive = false;
            cmd->backCmd[i].oldCmd[0] = '\0';
            allocateBackMembers(&cmd->backCmd[i], cmd->argvNum);
        }
        cmd->backCmdCount = newCount;
    }
}

void freeBackCmd(struct Command *cmd) {
    if (cmd->backCmd != NULL) {
        //free pid
        if (cmd->backCmd->backChildStatus != NULL) {
            free(cmd->backCmd->backChildStatus);
            cmd->backCmd->backChildStatus = NULL;
        }
        //free status
        if (cmd->backCmd->backChildPids != NULL) {
            free(cmd->backCmd->backChildPids);
            cmd->backCmd->backChildPids = NULL;
        }
        //struct
        free(cmd->backCmd);
        cmd->backCmd = NULL;
        cmd->backCmdCount = 0;
    }
}

void fixNullEntries(char **argv) {
    //reinitialize null data
    for (size_t i = 0; i < NON_NULL_MAX; i++) {
        if (argv[i] == NULL) {
            argv[i] = calloc(TOKEN_MAX, sizeof(char));
        }
    }
}

int outRedirect(char *file) {
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        return OUTPUT_FILE_OPEN_ERR;
    }
    //links file to stdout
    dup2(fd, STDOUT_FILENO);
    close(fd);
    return 0;
}

int inRedirect(char *file) {
    int fd = open(file, O_RDONLY, 0644);
    if (fd == -1) {
        return INPUT_FILE_OPEN_ERR;
    }
    //links file to stdin
    dup2(fd, STDIN_FILENO);
    close(fd);
    return 0;
}

int missingCMDParse(struct Command *cmd, size_t *i) {
    if (cmd->cmd[*i+1] == '\0') {
        return -1;
    } else if (cmd->cmd[*i+1] == ' ') {
        (*i)++; // at ' '
        while (cmd->cmd[*i+1] == ' ') (*i)++;
        if (cmd->cmd[*i+1] == '\0') return -1;
    }
    return 0;
}

int parseCommand(struct Command *cmd) {
    size_t i = 0, j = 0, k = 0; 
    bool commandStarted = false; //check if a command started for the current pipe segment
    //parses command into tokens
    while (i < CMDLINE_MAX && cmd->cmd[i] != '\0') {
        //kills "" && ''
        if ((cmd->cmd[i] == '\'' || cmd->cmd[i] == '"') && j != 0) {
            i++; continue;
        }

        //background jobs
        if (cmd->cmd[i] == '&') {
            cmd->isBackJob = true;
            i++; continue;
        }

        //input redirection 
        if (cmd->cmd[i] == '<') {
            //makes sure theres cmd before output redir
            if (j == 0 && k == 0 && cmd->argvNum < 1) return MISSING_CMD_ERR;
            //makes sure theres output file
            if (missingCMDParse(cmd, &i) == -1) return INPUT_RDIR_ERR;
            //cannot have a pipe before input redirection
            if (cmd->argvNum >= 1) return INPUT_LOCATION_ERR;
            
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }
            
            i++; 
            //store the input filename
            while(cmd->cmd[i] == ' ') i++; //skip leading WS after <
            size_t start = i; //starting index of input filename
            while(cmd->cmd[i] != '\0' && cmd->cmd[i] != ' ') i ++; //increment index until end of stirng
            size_t len = i - start; //len of input filename
            char* newInputFile = malloc(len + 1);
            if (!newInputFile) return -1; //malloc error
            strncpy(newInputFile, &cmd->cmd[start], len); //copy from command string to inputFile
            newInputFile[len] = '\0'; 

            if(cmd->inputFile){ //if another file exists
                free(cmd->inputFile);
            }
            cmd->inputFile = newInputFile; //want to use the most recently opened file 
            continue;
        }

        //output redirection
        if (cmd->cmd[i] == '>') {
            //makes sure theres cmd before output redir
            if (j == 0 && k == 0 && cmd->argvNum < 1) return MISSING_CMD_ERR;
            //makes sure theres output file
            if (missingCMDParse(cmd, &i) == -1) return OUTPUT_RDIR_ERR;
            
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }
            
            i++; 
            //store output filename
            while (cmd->cmd[i] == ' ') i++;
            size_t start = i;
            while (cmd->cmd[i] != '\0' && cmd->cmd[i] != ' ') i++;
            size_t len = i - start;
            char* newOutputFile = malloc(len + 1);
            if (!newOutputFile) return -1; //malloc err
            strncpy(newOutputFile, &cmd->cmd[start], len);
            newOutputFile[len] = '\0';

            if(cmd->outputFile){
                free(cmd->outputFile);
            }
            cmd->outputFile = newOutputFile;
            continue;
        }

        //pipes
        if (cmd->cmd[i] == '|') {
            //makes sure theres cmd before pipe
            if (!commandStarted) return MISSING_CMD_ERR;
            //makes sure theres cmd after pipe
            if (missingCMDParse(cmd, &i) == -1) return MISSING_CMD_ERR;
            //cannot pipe after background
            if (cmd->isBackJob) return BACK_JOB_LOCATION_ERR;
            //cannot pipe after ouput
            if (cmd->outputFile) return OUTPUT_LOCATION_ERR; 
            
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
            fixNullEntries(cmd->argv);
            j = 0; k = 0; i++;
            commandStarted = false;
            continue;
        }

        //whitespace
        if (cmd->cmd[i] == ' ') {
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }
            //kills multi whitespace
            while (cmd->cmd[i+1] == ' ') i++;
            i++;
            continue;
        }
        
        //too many arguments
        if (j >= NON_NULL_MAX) return MAX_ARG_ERR;
        //individual token too big
        if (k >= TOKEN_MAX) return MAX_ARG_SIZE_ERR;
        
        //puts cmd into argv
        cmd->argv[j][k] = cmd->cmd[i];
        k++; i++; //goes to new token
        commandStarted = true;
    }

    //output redirecting -- changes file w terminal
    if(cmd->outputFile) {
        int oRedirSuccess = outRedirect(cmd->outputFile);
        free(cmd->outputFile);
        cmd->outputFile = NULL; //reset
        if (oRedirSuccess == OUTPUT_FILE_OPEN_ERR) return OUTPUT_FILE_OPEN_ERR;
    }
    if (cmd->inputFile) {
        int iRedirSuccess = inRedirect(cmd->inputFile);
        free(cmd->inputFile);
        cmd->inputFile = NULL; //reset
        if (iRedirSuccess == INPUT_FILE_OPEN_ERR) return INPUT_FILE_OPEN_ERR;
    }

    //ends line and puts NULL
    if (k > 0) {
        cmd->argv[j][k] = '\0';
        cmd->argv[j+1] = NULL;
        cmd->argvNum++;
    } else {
        cmd->argv[j] = NULL;
        cmd->argvNum++;
    }
    //puts the argv into the list 
    if (cmd->argvList.count < cmd->argvNum) {
        pushArgv(&cmd->argvList, cmd->argv);
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

int createBackCmd(struct Command *cmd) {
    int i = 0;
    //free - reusable - backCmd 
    for (; i < cmd->backCmdCount; i++) {
        if (cmd->backCmd[i].isActive == false) {
            strcpy(cmd->backCmd[i].oldCmd, cmd->cmd);
            cmd->backCmd[i].isActive = true;
            return i;
        }
    }

    //needs more
    allocateBackCmd(cmd);
    i++;
    strcpy(cmd->backCmd[i].oldCmd, cmd->cmd);
    cmd->backCmd[i].isActive = true;
    return i;
}

void checkBackCmds(struct Command *cmd) {
    for (int i = 0; i < cmd->backCmdCount; i++) {
        if (cmd->backCmd[i].isActive) {
            int childCount; bool oneSuccess = true;
            //
            for (childCount = 0; cmd->backCmd[i].backChildPids[childCount] != -1; childCount++) {
                int status;
                pid_t ret = waitpid(cmd->backCmd[i].backChildPids[childCount], &status, WNOHANG);
                if (ret == cmd->backCmd[i].backChildPids[childCount]) {
                    if (WIFEXITED(status)) {
                        cmd->backCmd[i].backChildStatus[childCount] = WEXITSTATUS(status);
                    }
                } else {
                    oneSuccess = false;
                }
            }
            //
            if (oneSuccess) {
                fprintf(stderr, "+ completed '%s' ", cmd->backCmd[i].oldCmd);
                for (int j = 0; j < childCount; j++) {
                    fprintf(stderr, "[%d]", cmd->backCmd[i].backChildStatus[j]);
                }
                fprintf(stderr, "\n");
                cmd->backCmd[i].isActive = false;
            }
        }
    }
}

void waitForForks(struct Command *cmd) {
    //if background cmd - just store the PID for later check
    if (cmd->isBackJob) {
        int backJobIndex = createBackCmd(cmd);
        for (int i = 0; i < cmd->argvNum; i++) {
            cmd->backCmd[backJobIndex].backChildPids[i] = cmd->pidChilds[i];
        }
        cmd->backCmd[backJobIndex].backChildPids[cmd->argvNum] = -1;
        return;
    }

    //normal cmd
    int childStatusList[cmd->argvNum];
    //get the current child's status
    for (int i = 0; i < cmd->argvNum; i++) {
        int status;
        waitpid(cmd->pidChilds[i], &status, 0);
        childStatusList[i] = WEXITSTATUS(status);
    }

    //check for background finishes
    checkBackCmds(cmd); //!if you want to print &cmd print before cmd 

    //print completion code
    fprintf(stderr, "+ completed '%s' ", cmd->cmd);
    for(int i = 0; i < cmd->argvNum; i++){
        fprintf(stderr, "[%d]", childStatusList[i]);
    }
    fprintf(stderr, "\n");
}

bool canExit(struct Command *cmd) {
    for (int i = 0; i < cmd->backCmdCount; i++) {
        if (cmd->backCmd[i].isActive) return false; 
    }
    return true;
}

//build in cmd
int builtInCmd(struct Command *cmd) {
    int cmdNum = 0;
    ArgvNode* cur = cmd->argvList.head;
    while (cmdNum < cmd->argvNum) {
        //EXIT
        if (!strcmp(cur->argv[0], "exit")) {
            //checks if can exit
            if (!canExit(cmd)) {
                fprintf(stderr, "Error: active job still running\n");
                fprintf(stderr, "+ completed '%s' [1]\n", cmd->cmd);
                return EXIT_FAIL;
            }
            //exits
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
        cmdNum++;
        cur = cur->next;
    }
    return 0;
}

void errorMsgs(int parseResult) {
    if(parseResult == MAX_ARG_SIZE_ERR) {
        fprintf(stderr, "Error: Argument too long\n");
    } else if (parseResult == MAX_ARG_ERR) {
        fprintf(stderr, "Error: too many process arguments\n");
    } else if (parseResult == MISSING_CMD_ERR) {
        fprintf(stderr, "Error: missing command\n");
    } else if (parseResult == BACK_JOB_LOCATION_ERR) {
        fprintf(stderr, "Error: mislocated background sign\n");
    } else if (parseResult == OUTPUT_RDIR_ERR) {
        fprintf(stderr, "Error: no output file\n");
    } else if (parseResult == OUTPUT_FILE_OPEN_ERR) {
        fprintf(stderr, "Error: cannot open output file\n");
    } else if (parseResult == OUTPUT_LOCATION_ERR) {
        fprintf(stderr, "Error: mislocated output redirection\n");
    } else if (parseResult == INPUT_LOCATION_ERR) {
        fprintf(stderr, "Error: mislocated input redirection\n");
    } else if (parseResult == INPUT_FILE_OPEN_ERR) {
        fprintf(stderr, "Error: cannot open input file\n");
    } else if (parseResult == INPUT_RDIR_ERR) {
        fprintf(stderr, "Error: no input file\n");
    }
}

void resetForNew(struct Command *cmd) {
    cmd->argvNum = 0; 
    fixNullEntries(cmd->argv);
    cmd->isBackJob = false;
    resetArgvList(&cmd->argvList);
    //
    dup2(cmd->terminalFd[0], STDIN_FILENO);
    dup2(cmd->terminalFd[1], STDOUT_FILENO);
}

void initializeForNew(struct Command *cmd) {
    allocateBackCmd(cmd);
    allocateChildPid(cmd);
    initializeList(&cmd->argvList);
    cmd->argv = allocateMemArgv(NON_NULL_MAX, TOKEN_MAX);
    cmd->terminalFd[0] = dup(STDIN_FILENO);
    cmd->terminalFd[1] = dup(STDOUT_FILENO);
}

int main() {
    //initialize 
    char *eof;
    struct Command command = {0};
    initializeForNew(&command);

    while (1) {
        //check for background jobs
        // checkBackCmds(&command); //!if you want to print &cmd print after cmd

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
            checkBackCmds(&command);
            continue; // prompt for input again
        }

        //parse input
        int parseResult = parseCommand(&command);
        command.currentArgv = command.argvList.head; //sets up list reading && index

        //command error handling
        if (parseResult < 0) {
            errorMsgs(parseResult);
            //resets n restarts
            resetForNew(&command);
            continue;
        }

        /* Builtin command -> requires parsing */ 
        int builtCmdResult = builtInCmd(&command);
        if (builtCmdResult == EXIT_STATUS) break;
        if (builtCmdResult == PWD_STATUS || builtCmdResult == CD_STATUS || 
            builtCmdResult == EXIT_FAIL) {
            resetForNew(&command);
            continue;
        }

        //syscall
        forkNExec(&command, 0, -1, 0);
        waitForForks(&command);

        //reset data
        resetForNew(&command);
    }
    //free everything before ending prog
    freeMemArgv(command.argv, NON_NULL_MAX);
    freeArgvList(&command.argvList);
    freeChildPid(&command);
    freeBackCmd(&command);
    //close fds
    close(command.terminalFd[0]);
    close(command.terminalFd[1]);
    return EXIT_SUCCESS;
}