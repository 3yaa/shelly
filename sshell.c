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
#define EXIT_FAIL -4
//
#define CMDLINE_ERR -5
#define MAX_ARG_ERR -6
#define INPUT_RDIR_ERR -7 //Error: no input file
#define OUTPUT_RDIR_ERR -8
#define MISSING_CMD_ERR -9
#define MAX_ARG_SIZE_ERR -10
#define INPUT_LOCATION_ERR -11 //Error: mislocated input redirection
#define OUTPUT_LOCATION_ERR -12
#define INPUT_FILE_OPEN_ERR -13 //Error: cannot open input file
#define OUTPUT_FILE_OPEN_ERR -14

typedef struct BackJob {
    bool isActive; //done - reuse
    int backChildStatus[36];
    pid_t backChildPids[36]; //!stores backJob's PID since rest is waiting on that
    char oldCmd[CMDLINE_MAX]; //stores the CMD of the background job
} BackCmd;

typedef struct Command {
    int argvNum; 
    char **argv;  
    bool isBackJob;
    ArgvList argvList;
    pid_t pidChilds[36]; //!
    ArgvNode *currentArgv;
    char cmd[CMDLINE_MAX]; //input cmd
    //
    int backCmdCount; 
    BackCmd *backCmd;
    //
    int terminalFd[2];
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

void fixNullEntries(char **argv) {
    //reinitialize null data
    for (size_t i = 0; i < NON_NULL_MAX; i++) {
        if (argv[i] == NULL) {
            argv[i] = calloc(TOKEN_MAX, sizeof(char));
        }
    }
}

int outRedirect(char *file) {
    // printf("--->%s\n", file);
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        return -1;
    }
    //links file to stdout
    dup2(fd, STDOUT_FILENO);
    close(fd);
    return 0;
}

int inRedirect(char *file) {
    int fd = open(file, O_RDONLY, 0644);
    if (fd == -1) {
        return -1;
    }
    //links file to stdout
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
    bool inRedirecting = false;
    bool outRedirecting = false; 
    //parses command into tokens
    while (i < CMDLINE_MAX && cmd->cmd[i] != '\0') {
        //too many arguments
        if (j >= NON_NULL_MAX-1) return MAX_ARG_ERR;
        //individual token too big
        if (k >= TOKEN_MAX-1) return MAX_ARG_SIZE_ERR;

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
            //
            if (cmd->argvNum >= 1) return INPUT_LOCATION_ERR;
            
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;
            }

            inRedirecting = true;
            i++; continue;
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
            if (outRedirecting) return OUTPUT_LOCATION_ERR; 
            
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
            continue;
        }

        //whitespace
        if (cmd->cmd[i] == ' ') {
            //ends word
            if (k > 0) {
                cmd->argv[j][k] = '\0';
                j++; k = 0;

                //output|input redirecting -- moves it back a word
                if (outRedirecting || inRedirecting) {
                    j = j-1; //goes back to the file word
                    // outRedirecting = false; //!breaks mislocated err
                    // int oRedirSuccess = outRedirect(cmd->argv[j]);
                    // if (oRedirSuccess == -1) return OUTPUT_FILE_OPEN_ERR;
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

    // printf("J:%ld | K:%ld", j, k);
    //output redirecting -- changes file w terminal
    if (outRedirecting) {
        outRedirecting = false;
        int oRedirSuccess = outRedirect(cmd->argv[j]);
        if (oRedirSuccess == -1) return OUTPUT_FILE_OPEN_ERR;
        k = 0; //goes back so it gets nullified
    }
    if (inRedirecting) {
        inRedirecting = false;
        int iRedirSuccess = inRedirect(cmd->argv[j]);
        if (iRedirSuccess == -1) return INPUT_FILE_OPEN_ERR;
        k = 0; //goes back so it gets nullified
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

void checkBackCmds(struct Command *cmd) {
    for (int i = 0; i < cmd->backCmdCount; i++) {
        if (cmd->backCmd[i].isActive) {
            int childCount; bool oneSuccess = true;
            //
            for (childCount = 0; cmd->backCmd[i].backChildPids[childCount] != -1; childCount++) {
                // printf("---->%d\n", cmd->backCmd[i].backChildPids[childCount]);
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

void allocateBackCmd(struct Command *cmd) {
    //initial allocation
    if (cmd->backCmd == NULL) {
        cmd->backCmd = malloc(5 * sizeof(BackCmd));
        if (cmd->backCmd == NULL) {
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }
        cmd->backCmdCount = 5;
        //initializes
        for (int i = 0; i < 5; i++) {
            cmd->backCmd[i].isActive = false;
            cmd->backCmd[i].oldCmd[0] = '\0';
        }
    } else {
        //resize
        int newCount = cmd->backCmdCount + 5;
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
        }
        cmd->backCmdCount = newCount;
    }
}

void freeBackCmd(struct Command *cmd) {
    if (cmd->backCmd != NULL) {
        free(cmd->backCmd);
        cmd->backCmd = NULL;
        cmd->backCmdCount = 0;
    }
}

int createBackCmd(struct Command *cmd) {
    //empty
    if (cmd->backCmd == NULL) {
        allocateBackCmd(cmd);
        return 0;
    }
    
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
    checkBackCmds(cmd);

    //print completion code
    fprintf(stderr, "+ completed '%s' ", cmd->cmd);
    for(int i = 0; i < cmd->argvNum; i++){
        fprintf(stderr, "[%d]", childStatusList[i]);
    }
    fprintf(stderr, "\n");
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

int main() {
    char *eof;
    struct Command command = {0};
    //initialize 
    allocateBackCmd(&command);
    initializeList(&command.argvList);
    command.argv = allocateMemArgv(NON_NULL_MAX, TOKEN_MAX);
    command.terminalFd[0] = dup(STDIN_FILENO);
    command.terminalFd[1] = dup(STDOUT_FILENO);

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
        int parseResult = parseCommand(&command);
        command.currentArgv = command.argvList.head; //sets up list reading && index

        //command error handling
        if(parseResult == MAX_ARG_SIZE_ERR){
            fprintf(stderr, "Error: Argument too long\n");
            return MAX_ARG_SIZE_ERR;
        } else if (parseResult == MAX_ARG_ERR){
            fprintf(stderr, "Error: Too many arguments\n");
            return MAX_ARG_ERR;
        } else if (parseResult == MISSING_CMD_ERR || parseResult == OUTPUT_FILE_OPEN_ERR ||
                   parseResult == OUTPUT_LOCATION_ERR || parseResult == OUTPUT_RDIR_ERR ||
                   parseResult == INPUT_RDIR_ERR || parseResult == INPUT_LOCATION_ERR ||
                   parseResult == INPUT_FILE_OPEN_ERR) {
                
            if (parseResult == MISSING_CMD_ERR) {
                fprintf(stderr, "Error: missing command\n");
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

        
        // ArgvNode *cur = command.argvList.head;
        // printf("*****\n");
        // while(cur) {
        //     for (int i = 0; cur->argv[i] != NULL;i++) {
        //         printf("%s ", cur->argv[i]);
        //     }
        //     printf("\n");
        //     cur = cur->next;
        // }


        //syscall
        forkNExec(&command, 0, -1, 0);
        waitForForks(&command);

        //reset data
        resetForNew(&command);
    }
    freeMemArgv(command.argv, NON_NULL_MAX);
    freeArgvList(&command.argvList);
    freeBackCmd(&command);
    //
    close(command.terminalFd[0]);
    close(command.terminalFd[1]);
    return EXIT_SUCCESS;
}