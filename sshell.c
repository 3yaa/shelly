#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
/* got Implicit declaration of function ‘wait’... got answer from 
https://stackoverflow.com/questions/41884685/implicit-declaration-of-function-wait*/
#include <sys/types.h>
#include <sys/wait.h>
//
#define CMDLINE_MAX 512
#define NON_NULL_MAX 16
#define TOKEN_MAX 32
// #define PIPE_MAX 3
//
#define CMDLINE_ERR -4
#define MAX_ARG_ERR -6
#define MAX_ARG_SIZE_ERR -7

struct Command {
    char cmd[CMDLINE_MAX];
    char *argv[NON_NULL_MAX];
    char **argvList;
    size_t argvNum;


    //
    char *argv2[NON_NULL_MAX];
    char *argv3[NON_NULL_MAX];
    // char *argumentList[MAX_PIPE];
};

int allocateMem(char **argv, int rows, int cols) {
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

void freeMemory(char **argv, int rows) {
    for (int i = 0; i < rows; i++) {
        if (argv[i]) {
            free(argv[i]);
            argv[i] = NULL;
        }
    }
}

int parseCommand(struct Command *cmd) {
    //reinitialize null data
    for (size_t i = 0; i < NON_NULL_MAX; i++) {
        if (cmd->argv[i] == NULL) {
            cmd->argv[i] = calloc(TOKEN_MAX, sizeof(char));
        }
    }

    //parses command into tokens
    size_t i = 0, j = 0, k = 0;
    while (i < CMDLINE_MAX && cmd->cmd[i] != '\0') {
        //individual token too big
        if (k >= TOKEN_MAX-1) {
            return MAX_ARG_SIZE_ERR;
        }
        //pipe
        if (cmd->cmd[i] == '|') {
            cmd->argv[j][k] = '\0';
            j++;
            cmd->argv[j] = "|\0";
            j++; k = 0;
        }

        //kills white space
        if (cmd->cmd[i] == ' ') {
            //too many arguments
            if (j >= NON_NULL_MAX-1) {
                return MAX_ARG_ERR;
            }
            //kills multi white spaces
            while (cmd->cmd[i+1] == ' ' && cmd->cmd[i+1] != '\0') {
                i++;
            }
            if (cmd->cmd[i+1] != '\0') i++; //skips the last whitespace
            if (j == 0 && k == 0) {
                continue;
            }
            cmd->argv[j][k] = '\0'; //ends the argument
            j++; k = 0; //goes to new argument
        }
        //kills "" && ''
        if ((cmd->cmd[i] == '\'' || cmd->cmd[i] == '"') && j != 0) {
            i++;
            continue;
        }
        //gets pipe
        if (cmd->cmd[i] == '|') {
            cmd->argvNum++;
        }
        cmd->argv[j][k] = cmd->cmd[i];
        k++; i++; //goes to new token
    }
    //ends w null termination
    if (k > 0) {
        cmd->argv[j][k] = '\0';
        cmd->argv[j+1] = NULL;
    } else if (k == 1) {
        cmd->argv[j] = NULL;
    }
    return 0;
}

void separateArgvs(struct Command *cmd) {
    if (cmd->argvNum <= 1) return;
    //
    size_t counter = 1, argvC_2 = 0, argvC_3 = 0; int i = 0;
    for (; cmd->argv[i] != NULL; i++) {
        //first command
        if (counter == 1) {
            if (*cmd->argv[i] == '|') {
                cmd->argv[i] = NULL;
                counter++;
            }
            continue;
        }
        //second command
        if (counter == 2) {
            if (*cmd->argv[i] == '|') {
                cmd->argv2[argvC_2] = NULL;
                counter++;
                continue;
            }
            cmd->argv2[argvC_2] = cmd->argv[i];
            argvC_2++;
            continue;
        }
        //third command
        if (counter == 3) {
            cmd->argv3[argvC_3] = cmd->argv[i];
            argvC_3++; 
        }
    }
    if (counter >= 2) {
        cmd->argv2[argvC_2] = NULL;
    }
    if (counter >= 3) {
        cmd->argv3[argvC_3] = NULL;
    }
}

void forkNExec(struct Command *cmd, size_t i) {
    if(i >= cmd->argvNum) return;

    // char** args_list[3] = {cmd->arguments, cmd->argument2, cmd->argument3};
    // char** args = args_list[i];

    pid_t pid1, pid2, pid3;
    int pipe1_fd[2];
    int pipe2_fd[2];

    //
    if (cmd->argvNum > 1) pipe(pipe1_fd);
    if (cmd->argvNum > 2) pipe(pipe2_fd);

    // First command
    pid1 = fork();
    if (pid1 == 0) {
        if (cmd->argvNum > 1) {
            close(pipe1_fd[0]);
            dup2(pipe1_fd[1], STDOUT_FILENO);
            close(pipe1_fd[1]);
        }
        execvp(cmd->argv[0], cmd->argv);
        fprintf(stderr, "Error: command not found\n");
        exit(1);
    }

    // Second command (if exists)
    if (cmd->argvNum >= 2) {
        pid2 = fork();
        if (pid2 == 0) {
            // Set up input from previous pipe
            close(pipe1_fd[1]);
            dup2(pipe1_fd[0], STDIN_FILENO);
            close(pipe1_fd[0]);
            
            // Set up output to next pipe if needed
            if (cmd->argvNum > 2) {
                close(pipe2_fd[0]);
                dup2(pipe2_fd[1], STDOUT_FILENO);
                close(pipe2_fd[1]);
            }
            
            execvp(cmd->argv2[0], cmd->argv2);
            fprintf(stderr, "Error: command not found\n");
            exit(1);
        }
        // Parent closes pipe ends it won't use
        close(pipe1_fd[0]);
        close(pipe1_fd[1]);
    }

    // Third command (if exists)
    if (cmd->argvNum > 2) {
        pid3 = fork();
        if (pid3 == 0) {
            close(pipe2_fd[1]);
            dup2(pipe2_fd[0], STDIN_FILENO);
            close(pipe2_fd[0]);
            
            execvp(cmd->argv3[0], cmd->argv3);
            fprintf(stderr, "Error: command not found\n");
            exit(1);
        }
        // Parent closes pipe ends it won't use
        close(pipe2_fd[0]);
        close(pipe2_fd[1]);
    }

    // Wait for all children to complete
    int status1, status2, status3;
    if (cmd->argvNum >= 3) {
        waitpid(pid3, &status3, 0);
        waitpid(pid2, &status2, 0);
        waitpid(pid1, &status1, 0);
        fprintf(stderr, "+ completed '%s' [%d][%d][%d]\n", cmd->cmd, 
            WEXITSTATUS(status1), WEXITSTATUS(status2), WEXITSTATUS(status3));
    } else if (cmd->argvNum == 2) {
        waitpid(pid2, &status2, 0);
        waitpid(pid1, &status1, 0);
        fprintf(stderr, "+ completed '%s' [%d][%d]\n", cmd->cmd, 
                WEXITSTATUS(status1), WEXITSTATUS(status2));
    } else {
        waitpid(pid1, &status1, 0);
        fprintf(stderr, "+ completed '%s' [%d]\n", cmd->cmd, 
            WEXITSTATUS(status1));
    }
}

void resizeArgvList(struct Command *cmd) {
    static size_t curSize = 0;

    char **temp = (char**)realloc(cmd->argvList, cmd->argvNum*sizeof(char*));
    //fail
    if (temp == NULL) return;
    //
    cmd->argvList = temp;
    //
    size_t i;
    for (i = curSize; i < cmd->argvNum; i++) {
        cmd->argvList[i] = (char*)calloc(CMDLINE_MAX, sizeof(char));
        //fail
        if (cmd->argvList[i] == NULL) {
            break;
        }
    }
    curSize = i;
}

int main(void) {
    struct Command command;
    command.argvNum = 2;
    resizeArgvList(&command);

    // for (size_t i = 0; command.argvList[i] != NULL; i++) {
    //     command.argvList[i] = "{i} \n";
    //     printf("%s\n", command.argvList[i]);
    // }

    command.argvList[0] = "0\n";

    // size_t row_size_bytes = sizeof(command.argvList[0]);
    // size_t element_size_bytes = sizeof(command.argvList[0][0]);
    // size_t num_cols = row_size_bytes / element_size_bytes;
    // printf("The row size (number of columns) is: %zu\n", num_cols);

    
    return 0;

    char *eof;
    allocateMem(command.argv, NON_NULL_MAX, TOKEN_MAX);
    allocateMem(command.argv2, NON_NULL_MAX, TOKEN_MAX);
    allocateMem(command.argv3, NON_NULL_MAX, TOKEN_MAX);

    while (1) {
        char *nl;

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

        command.argvNum = 1;

        /* Builtin command */
        if (!strcmp(command.cmd, "exit")) {
                fprintf(stderr, "Bye...\n");
                fprintf(stderr, "+ completed '%s' [0]\n", command.cmd); //need exit val
                break;
        }

        //parse input
        int parseResult = parseCommand(&command);
        if(parseResult == MAX_ARG_SIZE_ERR){
            fprintf(stderr, "Error: Argument too long\n");
        }
        else if (parseResult == MAX_ARG_ERR){
            fprintf(stderr, "Error: Too many arguments\n");
        }

        //separate multiple arguments
        separateArgvs(&command);

        //syscall
        forkNExec(&command, 0);
    }
    freeMemory(command.argv, NON_NULL_MAX);
    return EXIT_SUCCESS;
}