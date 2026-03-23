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
volatile int SIG_INT = 0;
int hanged_pids[65535];
int hanged_pipes[65535];
int *hanged_pids_end = hanged_pids;
int *hanged_pipes_end = hanged_pipes;
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
    if (input != -1 && input != STDIN_FILENO) {
        if (dup2(input, STDIN_FILENO) == -1) {
            perror("dup2");
            exit(1);
        }
        if (close(input) == -1) {
            perror("close");
            exit(1);
        }
    }
    if (output != STDOUT_FILENO) {
        if (dup2(output, STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(1);
        }
        if (close(output) == -1) {
            perror("close");
            exit(1);
        }
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
int run_commands(Command *commands, char **str, char *file, char *file_input,
                 char forget, int last) {
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
    char no_stdinout = last == -1;
    if (file_input != NULL) {
        last = open(file_input, O_CREAT | O_RDONLY, 0644);
        if (last == -1) {
            perror("open");
            exit(1);
        }
        *hanged_pids_end = last;
        hanged_pipes_end++;
    }
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
            if (commands[1] == NULL && str == NULL && file == NULL &&
                !no_stdinout) {
                p[1] = STDOUT_FILENO;
            }
            run_command(*commands, last, p[1]);
        }
        if (close(p[1]) == -1) {
            perror("close");
            exit(1);
        }
        last = p[0];
        *hanged_pids_end = pid;
        *hanged_pipes_end = last;
        hanged_pids_end++;
        hanged_pipes_end++;
        commands++;
        pipes++;
    }
    if (str != NULL) {
        int size = 0;
        int cap = BUF_SIZE;
        while (1) {
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
        if (fp == -1) {
            perror("open");
            exit(1);
        }
        char *s = malloc(BUF_SIZE);
        if (s == NULL) {
            perror("malloc");
            exit(1);
        }
        while (1) {
            int num = read(last, s, BUF_SIZE);
            if (num == -1) {
                perror("read");
                exit(1);
            }
            if (num == 0) {
                break;
            }
            if (write(fp, s, num) == -1) {
                perror("write");
                exit(1);
            }
        }
        if (close(fp) == -1) {
            perror("close");
            exit(1);
        }
        free(s);
    }
    pipes -= count;
    if (forget) {
        free(pipes);
        return last;
    }
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        if (SIG_INT) {
            if (kill(pid, 9) == -1) {
                perror("kill");
                exit(1);
            }
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                exit(1);
            }
        } else {
            perror("waitpid");
            exit(1);
        }
    }
    hanged_pids_end--;
    for (int i = 1; i < count; i++) {
        if (wait(0) == -1) {
            perror("wait");
            exit(1);
        }
        hanged_pids_end--;
    }
    for (int i = 0; i < count; i++) {
        if (close(pipes[i]) == -1) {
            perror("close");
            exit(1);
        }
        hanged_pipes_end--;
    }
    free(pipes);
    if (SIG_INT) {
        SIG_INT = 0;
        return 130;
    } else {
        if (!WIFEXITED(status)) {
            return 1;
        }
        return WEXITSTATUS(status);
    }
}
struct CommandReturn {
    Command *command;
    char *file;
    char *file_input;
    char forget;
    int *close_pipes;
    int length;
};
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
struct CommandReturn get_commands(char *line, char is_command) {
    Command *commands = malloc(sizeof(Command *) * (strlen(line) / 2 + 2));
    if (commands == NULL) {
        perror("malloc");
        exit(1);
    }
    struct CommandReturn ret;
    ret.file = NULL;
    ret.file_input = NULL;
    ret.forget = 0;
    ret.command = commands;
    ret.length = 0;
    ret.close_pipes = malloc(sizeof(int) * strlen(line));
    if (ret.close_pipes == NULL) {
        perror("malloc");
        exit(1);
    }
    ret.close_pipes[0] = -1;
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
    char *buf;
    struct CommandReturn c;
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
                        } else if (line[n] == '>') {
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
                    free_commands(commands);
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
                        } else if (line[n] == '<') {
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
                    free_commands(commands);
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
            run_commands(c.command, &buf, NULL, NULL, 0, STDIN_FILENO);
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
            int pipeo = run_commands(c.command, NULL, NULL, NULL, 1, -1);
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
            free_commands(c.command);
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
            run_commands(c.command, NULL, NULL, NULL, 1, p[0]);
            strcat(buf, "/dev/fd/");
            sprintf(str, "%d", p[1]);
            strcat(buf, str);
            int *close_pipes = ret.close_pipes;
            while (*close_pipes != -1) {
                close_pipes++;
            }
            *close_pipes = p[0];
            *(close_pipes + 1) = p[1];
            *(close_pipes + 2) = -1;
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
void handle_hanged() {
    for (int *p = hanged_pids; p < hanged_pids_end; p++) {
        int status;
        if (waitpid(*p, &status, WNOHANG) == -1) {
            perror("waitpid");
            exit(1);
        }
        if (!WIFEXITED(status)) {
            return;
        }
    }
    hanged_pids_end = hanged_pids;
    for (int *p = hanged_pipes; p < hanged_pipes_end; p++) {
        if (close(*p) == -1) {
            perror("close");
            exit(1);
        }
    }
    hanged_pipes_end = hanged_pipes;
}
void handler(int signo, siginfo_t *info, void *context) { SIG_INT = 1; }
int main() {
    struct sigaction act = {0};
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = &handler;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    int ret = 0;
    using_history();
    char *dir = get_current_dir_name();
    CURRENT_DIR = malloc(strlen(dir) + 1);
    if (CURRENT_DIR == NULL) {
        perror("malloc");
        return 1;
    }
    strcpy(CURRENT_DIR, dir);
    while (1) {
        int size = snprintf(NULL, 0, "%s %d ", CURRENT_DIR, ret) + 1;
        char *prompt = malloc(size);
        if (prompt == NULL) {
            return 1;
        }
        snprintf(prompt, size, "%s %d ", CURRENT_DIR, ret);
        char *line = readline(prompt);
        free(prompt);
        handle_hanged();
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
            ret = run_commands(commands.command, NULL, commands.file,
                               commands.file_input, commands.forget,
                               STDIN_FILENO);
            if (commands.forget) {
                ret = 0;
            } else {
                int *close_pipes = commands.close_pipes;
                while (*close_pipes != -1) {
                    if (close(*close_pipes) == -1) {
                        perror("close");
                        exit(1);
                    }
                    close_pipes++;
                }
            }
            free_commands(commands.command);
        }
        if (commands.file != NULL) {
            free(commands.file);
        }
        if (commands.file_input != NULL) {
            free(commands.file_input);
        }
        free(commands.close_pipes);
    }
}
