#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/wait.h>
char *get_current_dir_name(void);
typedef char *Arg;
typedef Arg *Command;
enum State { NONE, SINGLEQUOTE, DOUBLEQUOTE, ESCAPE };
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
Command *get_commands(char *line) {
    Command *commands = malloc(sizeof(Command *) * (strlen(line) / 2 + 2));
    if (commands == NULL) {
        exit(1);
    }
    int i = 0;
    commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
    if (commands[i] == NULL) {
        exit(1);
    }
    int j = 0;
    commands[i][j] = malloc(strlen(line) + 1);
    if (commands[i][j] == NULL) {
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
                    free_commands(commands);
                    exit(1);
                }
                commands[i][j] = malloc(strlen(line) + 1);
                if (commands[i][j] == NULL) {
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
                        free_commands(commands);
                        exit(1);
                    }
                }
                break;
            default:
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
        }
        last = *line;
        line = line + 1;
    }
    if (i == 0 && j == 0 && k == 0) {
        free(commands[i][j]);
        commands[i] = NULL;
        return commands;
    }
    commands[i][j][k] = '\0';
    commands[i][j + 1] = NULL;
    commands[i + 1] = NULL;
    if (state != NONE || last == '|') {
        free_commands(commands);
        return NULL;
    }
    return commands;
}
int run_command(Command command, int input, int output) {
    if ((input != STDIN_FILENO &&
         (dup2(input, STDIN_FILENO) == -1 || close(input) == -1)) ||
        (output != STDOUT_FILENO &&
         (dup2(output, STDOUT_FILENO) == -1 || close(output) == -1))) {
        exit(1);
    }
    execvp(command[0], command);
    exit(1);
}
int run_commands(Command *commands) {
    if (*commands == NULL) {
        return 0;
    }
    int last = STDIN_FILENO;
    int p[2];
    if (pipe(p) == -1) {
        exit(1);
    }
    int pid;
    while (*commands != NULL) {
        pid = fork();
        if (pid == -1) {
            exit(1);
        }
        if (pid == 0) {
            if (close(p[0]) == -1) {
                exit(1);
            }
            if (commands[1] == NULL) {
                p[1] = STDOUT_FILENO;
            }
            run_command(*commands, last, p[1]);
        }
        if (close(p[1]) == -1) {
            exit(1);
        }
        last = p[0];
        commands = commands + 1;
        if (commands[1] != NULL && pipe(p) == -1) {
            exit(1);
        }
    }
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) {
        exit(1);
    }
    while (wait(0) != -1) {
    };
    return WEXITSTATUS(status);
}
int main() {
    int ret = 0;
    using_history();
    while (true) {
        char *dir = get_current_dir_name();
        int size = snprintf(NULL, 0, "%s %d ", dir, ret) + 1;
        char *prompt = malloc(size);
        if (prompt == NULL) {
            return 1;
        }
        snprintf(prompt, size, "%s %d ", dir, ret);
        char *line = readline(prompt);
        free(dir);
        free(prompt);
        if (line == NULL) {
            return 1;
        }
        if (strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        add_history(line);
        Command *commands = get_commands(line);
        if (commands == NULL) {
            printf("ERROR\n");
        } else {
            ret = run_commands(commands);
            free_commands(commands);
        }
        free(line);
    }
    clear_history();
    return 0;
}
