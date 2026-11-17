#include "shell.h"

Job jobs[MAX_JOBS];
Variable *variables = NULL;

/* Store or update variable */
int set_variable(const char *name, const char *value) {
    Variable *current = variables;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            char *new_value = strdup(value);
            if (!new_value) return 0;

            free(current->value);
            current->value = new_value;
            return 1;
        }
        current = current->next;
    }

    Variable *new_variable = malloc(sizeof(Variable));
    if (!new_variable) return 0;

    new_variable->name = strdup(name);
    new_variable->value = strdup(value);

    if (!new_variable->name || !new_variable->value) {
        free(new_variable->name);
        free(new_variable->value);
        free(new_variable);
        return 0;
    }

    new_variable->next = variables;
    variables = new_variable;

    return 1;
}

/* Retrieve variable value */
char *get_variable(const char *name) {
    Variable *current = variables;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0)
            return current->value;

        current = current->next;
    }

    return "";
}

/* Display all variables */
void show_variables(void) {
    Variable *current = variables;

    while (current != NULL) {
        printf("%s=%s\n", current->name, current->value);
        current = current->next;
    }
}

/* Expand $VARIABLE arguments */
void expand_variables(char **args) {
    for (int i = 0; args[i] != NULL; i++) {

        if (args[i][0] == '$') {
            char *value = get_variable(args[i] + 1);
            char *expanded = strdup(value);

            if (expanded) {
                free(args[i]);
                args[i] = expanded;
            }
        }
    }
}

void add_job(pid_t pid, char* command) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].pid = pid;
            strncpy(jobs[i].command, command, MAX_LEN - 1);
            jobs[i].command[MAX_LEN - 1] = '\0';
            jobs[i].active = 1;
            printf("[Background] PID: %d\n", pid);
            return;
        }
    }

    printf("Job list is full!\n");
}

void reap_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].active && jobs[i].pid == pid) {
                printf("\n[Completed] PID: %d\n", pid);
                jobs[i].active = 0;
                break;
            }
        }
    }
}
/* Display all background jobs */
void show_jobs(void) {
    reap_jobs();

    printf("\nBackground Jobs:\n");

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            printf("PID: %d | Command: %s\n",
                   jobs[i].pid, jobs[i].command);
        }
    }

    printf("\n");
}


/* Handle built-in shell commands */
int handle_builtin(char **args) {

    if (!args || !args[0])
        return 1;

    /* Exit command */
    if (strcmp(args[0], "exit") == 0) {
        printf("Exiting shell...\n");
        exit(0);
    }

    /* Change directory */
    if (strcmp(args[0], "cd") == 0) {
        if (!args[1])
            fprintf(stderr, "cd: directory required\n");
        else if (chdir(args[1]) != 0)
            perror("cd");

        return 1;
    }

    /* Help command */
    if (strcmp(args[0], "help") == 0) {
        printf("\nBuilt-in Commands:\n");
        printf("  cd <dir>  - Change directory\n");
        printf("  exit      - Exit the shell\n");
        printf("  help      - Display available commands\n");
        printf("  jobs      - Display background jobs\n");
        printf("  history   - Display command history\n");
        printf("  set       - Display shell variables\n");
        return 1;
    }

    /* Display background jobs */
    if (strcmp(args[0], "jobs") == 0) {
        show_jobs();
        return 1;
    }

    /* Display command history */
    if (strcmp(args[0], "history") == 0) {
        printf("Use Readline history.\n");
        return 1;
    }

    /* Display shell variables */
    if (strcmp(args[0], "set") == 0) {
        show_variables();
        return 1;
    }

    return 0;
}
static void run_command(char** args, int input_fd, int output_fd) {

    if (input_fd != STDIN_FILENO) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }

    if (output_fd != STDOUT_FILENO) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }

    for (int i = 0; args[i] != NULL; i++) {

        if (strcmp(args[i], "<") == 0) {
            if (!args[i + 1]) exit(1);

            int fd = open(args[i + 1], O_RDONLY);

            if (fd < 0) {
                perror("input");
                exit(1);
            }

            dup2(fd, STDIN_FILENO);
            close(fd);
            args[i] = NULL;
            break;
        }

        if (strcmp(args[i], ">") == 0) {
            if (!args[i + 1]) exit(1);

            int fd = open(args[i + 1],
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (fd < 0) {
                perror("output");
                exit(1);
            }

            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
            break;
        }
    }

    if (!args[0]) exit(1);

    execvp(args[0], args);

    perror("Command execution failed");
    exit(127);
}

int execute(char** args, int background) {

    if (!args || !args[0]) return 0;

    char command[MAX_LEN] = "";

    for (int i = 0; args[i] != NULL; i++) {
        if (strlen(command) + strlen(args[i]) + 2 < MAX_LEN) {
            strcat(command, args[i]);
            strcat(command, " ");
        }
    }

    int pipe_pos = -1;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_pos = i;
            break;
        }
    }

    if (pipe_pos >= 0) {
        args[pipe_pos] = NULL;

        int fd[2];

        if (pipe(fd) == -1) {
            perror("pipe");
            return 1;
        }

        pid_t left = fork();

        if (left == 0) {
            close(fd[0]);
            run_command(args, STDIN_FILENO, fd[1]);
        }

        pid_t right = fork();

        if (right == 0) {
            close(fd[1]);
            run_command(&args[pipe_pos + 1], fd[0], STDOUT_FILENO);
        }

        close(fd[0]);
        close(fd[1]);

        waitpid(left, NULL, 0);
        waitpid(right, NULL, 0);

        return 0;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0)
        run_command(args, STDIN_FILENO, STDOUT_FILENO);

    if (background) {
        add_job(pid, command);
        return 0;
    }

    int status;

    waitpid(pid, &status, 0);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return 1;
}
