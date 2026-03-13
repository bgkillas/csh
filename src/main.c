#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/wait.h>
char *get_current_dir_name();
typedef char *Arg;
typedef Arg *Command;
void free_commands(Command *commands) {
    int i = 0;
    while (commands[i] != NULL) {
        int j = 0;
        while (commands[i][j] != NULL) {
            free(commands[i][j]);
            j++;
        }
        free(commands[i]);
        i++;
    }
    free(commands);
}
void run_command(Command command, int input, int output) {
    if ((input != STDIN_FILENO &&
         (dup2(input, STDIN_FILENO) == -1 || close(input) == -1)) ||
        (output != STDOUT_FILENO &&
         (dup2(output, STDOUT_FILENO) == -1 || close(output) == -1))) {
        exit(1);
    }
    execvp(*command, command);
    perror("execvp");
    exit(1);
}
int cmdlen(Command *commands) {
    int i = 0;
    while (commands[i] != NULL) {
        i++;
    }
    return i;
}
char *CURRENT_DIR;
int builtin(Command command) {
    if (strcmp(*command, "exit") == 0) {
        if (command[1] != NULL) {
            exit(strtol(command[1], NULL, 10));
        } else {
            exit(0);
        }
    } else if (strcmp(*command, "cd") == 0) {
        char *new_dir;
        if (command[1] != NULL) {
            new_dir = command[1];
        } else {
            new_dir = "/home";
        }
        int len = strlen(new_dir);
        if (strlen(CURRENT_DIR) > len) {
            free(CURRENT_DIR);
            CURRENT_DIR = malloc(len + 1);
            if (CURRENT_DIR == NULL) {
                perror("malloc");
                exit(1);
            }
        }
        strcpy(CURRENT_DIR, new_dir);
        chdir(new_dir);
        return 0;
    } else if (strcmp(*command, "sleep") == 0) {
        if (command[1] != NULL) {
            sleep(strtol(command[1], NULL, 10));
            return 0;
        } else {
            return 1;
        }
    } else if (strcmp(*command, "exec") == 0) {
        if (command[1] != NULL) {
            command++;
            execvp(*command, command);
            perror("execvp");
            exit(1);
        } else {
            return 1;
        }
    } else {
        return -1;
    }
}
int run_commands(Command *commands) {
    int count = cmdlen(commands);
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        int ret = builtin(*commands);
        if (ret != -1) {
            return ret;
        }
    }
    int *pipes = malloc(sizeof(int) * count);
    if (pipes == 0) {
        perror("malloc");
        exit(1);
    }
    int last = STDIN_FILENO;
    int p[2];
    int pid = 0;
    while (*commands != NULL) {
        if (pipe(p) == -1) {
            perror("pipe");
            exit(1);
        }
        *pipes = p[0];
        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            if (close(p[0]) == -1) {
                perror("close");
                exit(1);
            }
            if (commands[1] == NULL) {
                p[1] = STDOUT_FILENO;
            }
            run_command(*commands, last, p[1]);
        }
        if (close(p[1]) == -1) {
            perror("close");
            exit(1);
        }
        last = p[0];
        commands++;
        pipes++;
    }
    pipes -= count;
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(1);
    }
    if (!WIFEXITED(status)) {
        printf("unreachable\n");
        exit(1);
    }
    for (int i = 1; i < count; i++) {
        if (wait(0) == -1) {
            perror("wait");
            exit(1);
        }
    }
    for (int i = 0; i < count; i++) {
        if (close(pipes[i]) == -1) {
            perror("close");
            exit(1);
        }
    }
    free(pipes);
    return WEXITSTATUS(status);
}
enum State { NONE, SINGLEQUOTE, DOUBLEQUOTE, ESCAPE, COMMAND };
Command *get_commands(char *line, char is_command) {
    Command *commands = malloc(sizeof(Command *) * (strlen(line) / 2 + 2));
    if (commands == NULL) {
        perror("malloc");
        exit(1);
    }
    int i = 0;
    commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
    if (commands[i] == NULL) {
        perror("malloc");
        exit(1);
    }
    int j = 0;
    commands[i][j] = malloc(strlen(line) + 1);
    if (commands[i][j] == NULL) {
        perror("malloc");
        free_commands(commands);
        exit(1);
    }
    int k = 0;
    int last = '\0';
    enum State state = NONE;
    while (*line != '\0') {
        switch (state) {
        case NONE:
            switch (*line) {
            case '\\':
                state = ESCAPE;
                break;
            case '"':
                state = DOUBLEQUOTE;
                break;
            case '\'':
                state = SINGLEQUOTE;
                break;
            case '$':
                line++;
                state = COMMAND;
                break;
            case '|':
                commands[i][j][k] = '\0';
                if (last == '|' || last == '\0' || (last == ' ' && j == 0)) {
                    commands[i][j + 1] = NULL;
                    commands[i + 1] = NULL;
                    free_commands(commands);
                    return NULL;
                }
                k = 0;
                if (last == ' ') {
                    free(commands[i][j]);
                    commands[i][j] = NULL;
                } else {
                    commands[i][j + 1] = NULL;
                }
                j = 0;
                i++;
                commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
                if (commands[i] == NULL) {
                    perror("malloc");
                    free_commands(commands);
                    exit(1);
                }
                commands[i][j] = malloc(strlen(line) + 1);
                if (commands[i][j] == NULL) {
                    perror("malloc");
                    free_commands(commands);
                    exit(1);
                }
                break;
            case ' ':
                if (last != ' ' && last != '|' && last != '\0') {
                    commands[i][j][k] = '\0';
                    k = 0;
                    j++;
                    commands[i][j] = malloc(strlen(line) + 1);
                    if (commands[i][j] == NULL) {
                        perror("malloc");
                        free_commands(commands);
                        exit(1);
                    }
                }
                break;
            default:
                if (is_command && *line == ')') {
                    if (i == 0 && j == 0 && k == 0) {
                        free(commands[i][j]);
                        commands[i] = NULL;
                        return commands;
                    }
                    commands[i][j][k] = '\0';
                    if (last == ' ') {
                        free(commands[i][j]);
                        commands[i][j] = NULL;
                    } else {
                        commands[i][j + 1] = NULL;
                    }
                    commands[i + 1] = NULL;
                    if (state != NONE || last == '|') {
                        free_commands(commands);
                        return NULL;
                    }
                    return commands;
                }
                commands[i][j][k] = *line;
                k++;
                break;
            }
            break;
        case SINGLEQUOTE:
            if (*line == '\'') {
                state = NONE;
            } else {
                commands[i][j][k] = *line;
                k++;
            }
            break;
        case DOUBLEQUOTE:
            if (*line == '\"') {
                state = NONE;
            } else {
                commands[i][j][k] = *line;
                k++;
            }
            break;
        case ESCAPE:
            commands[i][j][k] = *line;
            k++;
            state = NONE;
            break;
        case COMMAND:
            // TODO
            break;
        }
        last = *line;
        line++;
    }
    if (i == 0 && j == 0 && k == 0) {
        free(commands[i][j]);
        commands[i] = NULL;
        return commands;
    }
    commands[i][j][k] = '\0';
    if (last == ' ') {
        free(commands[i][j]);
        commands[i][j] = NULL;
    } else {
        commands[i][j + 1] = NULL;
    }
    commands[i + 1] = NULL;
    if (state != NONE || last == '|') {
        free_commands(commands);
        return NULL;
    }
    return commands;
}
int main() {
    int ret = 0;
    using_history();
    char *dir = get_current_dir_name();
    CURRENT_DIR = malloc(strlen(dir) + 1);
    if (CURRENT_DIR == NULL) {
        perror("malloc");
        return 1;
    }
    strcpy(CURRENT_DIR, dir);
    while (true) {
        int size = snprintf(NULL, 0, "%s %d ", CURRENT_DIR, ret) + 1;
        char *prompt = malloc(size);
        if (prompt == NULL) {
            return 1;
        }
        snprintf(prompt, size, "%s %d ", CURRENT_DIR, ret);
        char *line = readline(prompt);
        free(prompt);
        if (line == NULL) {
            return 1;
        }
        add_history(line);
        Command *commands = get_commands(line, 0);
        free(line);
        if (commands == NULL) {
            printf("ERROR\n");
        } else {
            ret = run_commands(commands);
            free_commands(commands);
        }
    }
}
