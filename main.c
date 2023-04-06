/* necessary libraries */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/* constant variables */
#define MAX_LINE 80
#define MAX_BUFFER MAX_LINE/2 + 1
#define MAX_PIPE 3

/* define execute_command function for calls */
void execute_command(char **args, char **redir_argv, int *wait, int exit_status);

/* retrieves current working directory */
char* get_current_dir() {
    char* cwd = malloc(256 * sizeof(char)); // allocate memory on the heap
    if (cwd == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    if (getcwd(cwd, 256) == NULL) {
        perror("getcwd failed");
        exit(EXIT_FAILURE);
    }
    // find the last occurrence of '/' character in the path
    char* last_slash = strrchr(cwd, '/');
    if (last_slash != NULL) {
        // increment pointer to the character after '/'
        last_slash++;
        // move the current working directory name to the beginning of the string
        memmove(cwd, last_slash, strlen(last_slash) + 1);
    }
    return cwd;
}


/* basic implementation of the cd command in a shell */
int shell_cd(char **argv) {
    if (argv[1] == NULL) {
        fprintf(stderr, "Error: Expected argument after use of cd\n");
    } else {
        // Change the process's working directory
        if (chdir(argv[1]) != 0) {
            perror("Error: Failure to change the process's working directory\n");
        }
    }
    return 1;
}

/* removes the end-of-line character (\n) from the input */
void remove_new_line(char *line) {
    int i = 0;
    while (line[i] != '\n') {
        i++;
    }

    line[i] = '\0';
}

/* reads a line of input and stores it in the line buffer */
void read_line(char *line) {
    char *ret = fgets(line, MAX_LINE, stdin);

    
    remove_new_line(line);
}

/* parses the input string into tokens and stores each token in the argv array */
void parse_command(char *input_string, char **argv, int *wait) {
    int i = 0;
    char *token;

    // initialize argv to NULL
    for (i = 0; i < MAX_BUFFER; i++) {
        argv[i] = NULL;
    }

    // check for "&" at the end of input string
    if (input_string[strlen(input_string) - 1] == '&') {
        *wait = 0;
        input_string[strlen(input_string) - 1] = '\0';
    } else {
        *wait = 1;
    }

    // tokenize input_string and store tokens in argv
    i = 0;
    token = strtok(input_string, " ");
    while (token != NULL) {
        argv[i++] = token;
        token = strtok(NULL, " ");
    }
    argv[i] = NULL;
}

/* executes a given command with arguments by parsing and executing it */
int execute_command_from_history(char* command, char** redir_args) {
    // Allocate memory for storing arguments
    char* args[MAX_BUFFER] = {NULL};

    // Parse command into arguments
    int wait = 1;
    parse_command(command, args, &wait);

    // Execute command with given arguments
    int exit_status = 0;
    execute_command(args, redir_args, &wait, exit_status);

    return exit_status;
}

/* checks if there are commands in history, prints them and then executes the command by calling the  execute_command_from_history function */
int shell_history(char* history, char** redir_args) {
    if (strlen(history) == 0) {
        fprintf(stderr, "No commands in history\n");
        return 1;
    }

    // execute command from history
    printf("%s\n", history);
    int exit_status = execute_command_from_history(history, redir_args);

    return exit_status;
}

/* checks if there the command is a redirection operation */
int check_redirect(char **argv) {
    int i = 0;
    while (argv[i] != NULL) {
        if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "<") == 0) {
            return i; //there is redirect, returns first occurence of redirect symbol
        }
        i++;
    }
    return 0; // there is no redirect, returns 0
}

/* checks if command contains pipe character */
int check_pipe(char **argv) {
    int i = 0;
    while (argv[i] != NULL) {
        if (strcmp(argv[i], "|") == 0) {
            return i; // there is a pipe, returns index of pipe character
        }
        i++;
    }
    return 0; // no pipe, return 0
}

/* executes the command and its arguments, and if the execution fails, it displays an error message and exits with a failure status */
void execute_child(char **argv) {
    if (execvp(argv[0], argv) < 0) {
        fprintf(stderr, "Error: Failed to execte command\n");
        exit(EXIT_FAILURE);
    }
}

/* checks if there is a redirect symbol in the given argument array, creates a new argument array excluding the redirect symbols, sets up the redirection of input/output, and executes the command. */
int shell_redirect(char **args, char **redir_argv) {
    int redir_op_index = check_redirect(args);
    if (redir_op_index != 0) {
        redir_argv[0] = strdup(args[redir_op_index]);
        redir_argv[1] = strdup(args[redir_op_index + 1]);
        args[redir_op_index] = NULL;
        args[redir_op_index + 1] = NULL;
        if (strcmp(redir_argv[0], ">") == 0) {
            int fd_out;
            fd_out = creat(redir_argv[1], S_IRWXU);
            if (fd_out == -1) {
                perror("Error: Redirect output failed\n");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            if (close(fd_out) == -1) {
                perror("Error: Closing output failed\n");
                exit(EXIT_FAILURE);
            }
            execute_child(args);
        } else if (strcmp(redir_argv[0], "<") == 0) {
            int fd_in = open(redir_argv[1], O_RDONLY);
            if (fd_in == -1) {
                perror("Error: Redirect input failed\n");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            if (close(fd_in) == -1) {
                perror("Error: Closing input failed");
                exit(EXIT_FAILURE);
            }
            execute_child(args);
        }
        return 1;
    }
    return 0;
}

/* parses arguments, creates the pipe and forks two child processes, and closes the file descriptors for the pipe and waits for both child processes to finish */
int shell_pipe(char **args) {
    int pipe_op_index = check_pipe(args);
    if (pipe_op_index == 0) {
        return 0;
    }

    char *child1_args[MAX_PIPE];
    char *child2_args[MAX_PIPE];
    int i = 0;

    // Copy arguments before pipe symbol to child1_args
    while (args[i] != NULL && i < pipe_op_index) {
        child1_args[i] = strdup(args[i]);
        i++;
    }
    child1_args[i] = NULL;

    // Copy arguments after pipe symbol to child2_args
    int j = 0;
    i++; // Skip over pipe symbol
    while (args[i] != NULL) {
        child2_args[j] = strdup(args[i]);
        i++;
        j++;
    }
    child2_args[j] = NULL;

    // Create pipe
    int fd[2];
    if (pipe(fd) == -1) {
        perror("Error: Pipe failed\n");
        exit(EXIT_FAILURE);
    }

    // Fork child processes
    int child1_pid = fork();
    if (child1_pid == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        execute_child(child1_args);
        exit(EXIT_SUCCESS);
    }

    int child2_pid = fork();
    if (child2_pid == 0) {
        dup2(fd[0], STDIN_FILENO);
        close(fd[1]);
        close(fd[0]);
        execute_child(child2_args);
        exit(EXIT_SUCCESS);
    }

    // Wait for child processes to finish
    close(fd[0]);
    close(fd[1]);
    waitpid(child1_pid, NULL, 0);
    waitpid(child2_pid, NULL, 0);
    
    return 1;
}

/* execute a command received as input */
void execute_command(char **args, char **redir_argv, int *wait, int exit_status) {
    //handles change directory command
    if (strcmp(args[0], "cd") == 0) {
        exit_status = shell_cd(args);
    }
    
    //if no builtin command was executed successfully
    if (exit_status == 0) {
        int status;

        // create child process
        pid_t pid = fork();
        if (pid == 0) {
            // child process
            if (exit_status == 0) {
                exit_status = shell_redirect(args, redir_argv);
            }
            if (exit_status == 0) {
                exit_status = shell_pipe(args);
            }
            if (exit_status == 0) {
                execvp(args[0], args);
            }
            exit(EXIT_SUCCESS);

        } else if (pid < 0) { // child process creation failed
            perror("Error: Error forking\n");
            exit(EXIT_FAILURE);
        } else { // background execution
            // parent process
            if (wait) {
                // wait for the child process to terminate
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    // child process terminated normally
                    exit_status = WEXITSTATUS(status);
                }
            }
        }
    }
}

/* main function */
int main(void) {
    // array of command arguments
    char *command_args[MAX_LINE/2 + 1];

    // string for user input
    char input_string[MAX_LINE];

    // copy of user input string
    char copied_input_string[MAX_LINE];

    // previous command entered by user
    char previous_command[MAX_LINE] = "No commands in history\n";

    // array for input/output redirection
    char *redir_args[MAX_LINE];

    // flag indicating whether to wait for child process to finish
    int wait_flag;

    // exit status of executed command
    int exit_status = 0;

    // condition for shell infinite loop
    int should_run = 1;

    // initialize infinite loop for shell input commands
    while (should_run) {
        char* cwd = get_current_dir();
        printf("osh:%s> ", cwd);
        free(cwd);
        fflush(stdout);

        // read input string from user
        read_line(input_string);

        // copy input string
        strcpy(copied_input_string, input_string);

        // parse input string
        parse_command(input_string, command_args, &wait_flag);

        // execute command
        if (strcmp(command_args[0], "!!") == 0) {
            exit_status = shell_history(previous_command, redir_args);
        } else if (strcmp(command_args[0], "") == 0) {
            continue;
        } else if (strcmp(command_args[0], "exit") == 0) {
            break;
        }else {
            strcpy(previous_command, copied_input_string);
            execute_command(command_args, redir_args, &wait_flag, exit_status);
        }
        exit_status = 0;
    }
    return 0;
}
