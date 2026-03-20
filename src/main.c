#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <fcntl.h>
char *get_current_dir_name();
typedef char *Arg;
typedef Arg *Command;
int BUF_SIZE = 65535;
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
int run_commands(Command *commands, char **str, char *file) {
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
            if (commands[1] == NULL && str == NULL && file == NULL) {
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
    if (str != NULL) {
        int size = 0;
        int cap = BUF_SIZE;
        while (true) {
            int num = read(last, *str + size, BUF_SIZE);
            if (num == -1) {
                perror("read");
                exit(1);
            }
            if (num == 0) {
                break;
            }
            size += num;
            if (size + BUF_SIZE >= cap) {
                cap += BUF_SIZE;
                *str = realloc(*str, cap);
                if (*str == NULL) {
                    perror("realloc");
                    exit(1);
                }
            }
        }
        (*str)[size] = '\0';
    }
    if (file != NULL) {
        int fp = open(file, O_CREAT | O_WRONLY, 0644);
        char *s = malloc(BUF_SIZE);
        if (s == NULL) {
            perror("malloc");
            exit(1);
        }
        while (true) {
            int num = read(last, s, BUF_SIZE);
            if (num == -1) {
                perror("read");
                exit(1);
            }
            if (num == 0) {
                break;
            }
            write(fp, s, num);
        }
    }
    pipes -= count;
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
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
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}
struct CommandReturn {
    Command *command;
    char *file;
    int length;
};
enum State { NONE, SINGLEQUOTE, DOUBLEQUOTE, ESCAPE, SPECIAL, COMMAND, FILEO };
struct CommandReturn get_commands(char *line, char is_command) {
    Command *commands = malloc(sizeof(Command *) * (strlen(line) / 2 + 2));
    struct CommandReturn ret;
    ret.file = NULL;
    ret.command = commands;
    ret.length = 0;
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
            case '>':
                state = FILEO;
                break;
            case '"':
                state = DOUBLEQUOTE;
                break;
            case '\'':
                state = SINGLEQUOTE;
                break;
            case '$':
                state = SPECIAL;
                break;
            case '|':
                commands[i][j][k] = '\0';
                if (last == '|' || last == '\0' || (last == ' ' && j == 0)) {
                    commands[i][j + 1] = NULL;
                    commands[i + 1] = NULL;
                    free_commands(commands);
                    ret.command = NULL;
                    return ret;
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
                        return ret;
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
                        ret.command = NULL;
                        return ret;
                    }
                    return ret;
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
        case SPECIAL:
            if (*line == '(') {
                state = COMMAND;
            } else {
                free_commands(commands);
                ret.command = NULL;
                return ret;
            }
            break;
        case FILEO:
            ret.file = malloc(strlen(line) + 1);
            if (ret.file == NULL) {
                perror("malloc");
                free_commands(commands);
                exit(1);
            }
            strcpy(ret.file, line);
            line = "\0\0";
            state = NONE;
            break;
        case COMMAND:
            struct CommandReturn c = get_commands(line, 1);
            line += c.length;
            state = NONE;
            char *buf = malloc(BUF_SIZE);
            if (buf == NULL) {
                perror("malloc");
                exit(1);
            }
            *buf = '\0';
            run_commands(c.command, &buf, NULL);
            if (strlen(buf) >= c.length) {
                commands[i][j] = realloc(
                    commands[i][j], ret.length + strlen(line) + strlen(buf));
                if (commands[i][j] == 0) {
                    perror("realloc");
                    exit(1);
                }
            }
            commands[i][j][k] = '\0';
            strcat(commands[i][j], buf);
            k += strlen(buf);
            free(buf);
            free_commands(c.command);
            break;
        }
        last = *line;
        line++;
        ret.length++;
    }
    if (i == 0 && j == 0 && k == 0) {
        free(commands[i][j]);
        commands[i] = NULL;
        return ret;
    }
    commands[i][j][k] = '\0';
    if (last == ' ') {
        free(commands[i][j]);
        commands[i][j] = NULL;
    } else {
        commands[i][j + 1] = NULL;
    }
    commands[i + 1] = NULL;
    if (state != NONE || last == '|' || is_command) {
        free_commands(commands);
        ret.command = NULL;
        return ret;
    }
    return ret;
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
        if (strlen(line) != 0) {
            add_history(line);
        }
        struct CommandReturn commands = get_commands(line, 0);
        free(line);
        if (commands.command == NULL) {
            printf("ERROR\n");
        } else {
            ret = run_commands(commands.command, NULL, commands.file);
            free_commands(commands.command);
        }
        if (commands.file != NULL) {
            free(commands.file);
        }
    }
}
