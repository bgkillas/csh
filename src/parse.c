#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "main.h"
#include "parse.h"
#include "run.h"
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
void free_commands_ret(CommandReturn commands) {
    if (commands.command != NULL) {
        free_commands(commands.command);
    }
    if (commands.file != NULL) {
        free(commands.file);
    }
    if (commands.file_input != NULL) {
        free(commands.file_input);
    }
    int **close_pipes = commands.close_pipes;
    while (*close_pipes != (int *)-1) {
        if (*close_pipes != NULL) {
            free(*close_pipes);
        }
        close_pipes++;
    }
    free(commands.close_pipes);
}
enum State {
    NONE,
    SINGLEQUOTE,
    DOUBLEQUOTE,
    ESCAPE,
    SPECIAL,
    COMMAND,
    COMMANDFILE,
    COMMANDFILEINPUT,
    FILEO,
    FILEI
};
CommandReturn get_commands(char *line, char is_command) {
    Command *commands = malloc(sizeof(Command *) * (strlen(line) / 2 + 2));
    if (commands == NULL) {
        perror("malloc");
        exit(1);
    }
    CommandReturn ret;
    ret.file = NULL;
    ret.file_input = NULL;
    ret.forget = 0;
    ret.command = commands;
    ret.length = 0;
    ret.close_pipes = malloc(sizeof(int) * (strlen(line) / 2 + 2));
    if (ret.close_pipes == NULL) {
        perror("malloc");
        exit(1);
    }
    int i = 0;
    ret.close_pipes[i] = NULL;
    commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
    if (commands[i] == NULL) {
        perror("malloc");
        exit(1);
    }
    int j = 0;
    commands[i][j] = malloc(strlen(line) + 1);
    if (commands[i][j] == NULL) {
        perror("malloc");
        exit(1);
    }
    int k = 0;
    int last = '\0';
    enum State state = NONE;
    char *buf;
    CommandReturn c;
    char str[32];
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
            case '&':
                ret.forget = 1;
                line = "\0\0";
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
            case '<':
                state = FILEI;
                break;
            case '|':
                commands[i][j][k] = '\0';
                if (last == '|' || last == '\0' || (last == ' ' && j == 0)) {
                    commands[i][j + 1] = NULL;
                    commands[i + 1] = NULL;
                    ret.close_pipes[i + 1] = (int *)-1;
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
                ret.close_pipes[i] = NULL;
                commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
                if (commands[i] == NULL) {
                    perror("malloc");
                    exit(1);
                }
                commands[i][j] = malloc(strlen(line) + 1);
                if (commands[i][j] == NULL) {
                    perror("malloc");
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
                        exit(1);
                    }
                }
                break;
            default:
                if (is_command && *line == ')') {
                    if (i == 0 && j == 0 && k == 0) {
                        free(commands[i][j]);
                        free(commands[i]);
                        commands[i] = NULL;
                        ret.close_pipes[i] = (int *)-1;
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
                    ret.close_pipes[i + 1] = (int *)-1;
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
        case FILEI:
            if (*line == '(') {
                state = COMMANDFILEINPUT;
            } else {
                int n = 0;
                char escape = 0;
                while (line[n] != '\0') {
                    if (!escape) {
                        if (line[n] == '\\') {
                            escape = 1;
                        } else if (line[n] == '>' || line[n] == '&') {
                            break;
                        }
                    } else {
                        escape = 0;
                    }
                    n += 1;
                }
                ret.file_input = malloc(n + 1);
                if (ret.file_input == NULL) {
                    perror("malloc");
                    exit(1);
                }
                strncpy(ret.file_input, line, n);
                ret.file_input[n] = '\0';
                line += n - 1;
                state = NONE;
            }
            break;
        case FILEO:
            if (*line == '(') {
                state = COMMANDFILE;
            } else {
                int n = 0;
                char escape = 0;
                while (line[n] != '\0') {
                    if (!escape) {
                        if (line[n] == '\\') {
                            escape = 1;
                        } else if (line[n] == '<' || line[n] == '&') {
                            break;
                        }
                    } else {
                        escape = 0;
                    }
                    n += 1;
                }
                ret.file = malloc(n + 1);
                if (ret.file == NULL) {
                    perror("malloc");
                    exit(1);
                }
                strncpy(ret.file, line, n);
                ret.file[n] = '\0';
                line += n - 1;
                state = NONE;
            }
            break;
        case COMMAND:
            c = get_commands(line, 1);
            line += c.length;
            state = NONE;
            buf = malloc(BUF_SIZE);
            if (buf == NULL) {
                perror("malloc");
                exit(1);
            }
            *buf = '\0';
            run_commands(c, &buf, STDIN_FILENO, -1);
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
            free_commands_ret(c);
            break;
        case COMMANDFILEINPUT:
            c = get_commands(line, 1);
            line += c.length;
            state = NONE;
            buf = malloc(32);
            if (buf == NULL) {
                perror("malloc");
                exit(1);
            }
            *buf = '\0';
            c.forget = 1;
            run_commands(c, NULL, -1, -1);
            PidClosePipes pipes = *(hanged_pids_end - 1);
            int pipeo = pipes.pipes[0];
            strcat(buf, "/dev/fd/");
            sprintf(str, "%d", pipeo);
            strcat(buf, str);
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
            free_commands_ret(c);
            break;
        case COMMANDFILE:
            c = get_commands(line, 1);
            line += c.length;
            state = NONE;
            buf = malloc(32);
            if (buf == NULL) {
                perror("malloc");
                exit(1);
            }
            *buf = '\0';
            int p[2];
            if (pipe(p) == -1) {
                perror("pipe");
                exit(1);
            }
            c.forget = 1;
            run_commands(c, NULL, p[0], p[1]);
            strcat(buf, "/dev/fd/");
            sprintf(str, "%d", p[1]);
            strcat(buf, str);
            if (ret.close_pipes[i] == NULL) {
                ret.close_pipes[i] = malloc((strlen(line) + 1) * sizeof(int));
                if (ret.close_pipes[i] == NULL) {
                    perror("malloc");
                    exit(1);
                }
                *ret.close_pipes[i] = -1;
            }
            int *close_pipes = ret.close_pipes[i];
            while (*close_pipes != -1) {
                close_pipes++;
            }
            if (close(p[0]) == -1) {
                perror("close");
                exit(1);
            }
            *close_pipes = p[1];
            *(close_pipes + 1) = -1;
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
            free_commands_ret(c);
            break;
        }
        last = *line;
        line++;
        ret.length++;
    }
    if (i == 0 && j == 0 && k == 0) {
        free(commands[i][j]);
        free(commands[i]);
        commands[i] = NULL;
        ret.close_pipes[i] = (int *)-1;
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
    ret.close_pipes[i + 1] = (int *)-1;
    if (state != NONE || last == '|' || is_command) {
        free_commands_ret(ret);
        ret.command = NULL;
        return ret;
    }
    return ret;
}
