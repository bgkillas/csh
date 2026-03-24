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
int BUF_SIZE = 65536;
volatile int SIG_INT = 0;
typedef struct PidClosePipes {
    int pid;
    int *pipes;
} PidClosePipes;
PidClosePipes hanged_pids[65536];
PidClosePipes *hanged_pids_end = hanged_pids;
typedef struct CommandReturn {
    Command *command;
    char *file;
    char *file_input;
    char forget;
    int *close_pipes;
    int length;
} CommandReturn;
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
    free(commands.close_pipes);
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
void handle_hanged() {
    char do_exit = 0;
    int status;
    for (PidClosePipes *p = hanged_pids_end - 1; p >= hanged_pids; p--) {
        if (p->pid != -1) {
            if (waitpid(p->pid, &status, WNOHANG) == -1) {
                perror("waitpid");
                exit(1);
            }
            if (WIFEXITED(status)) {
                if (do_exit) {
                    p->pid = -1;
                } else {
                    hanged_pids_end--;
                }
                if (p->pipes != NULL) {
                    int *close_pipes = p->pipes;
                    while (*close_pipes != -1) {
                        if (close(*close_pipes) == -1) {
                            perror("close");
                            exit(1);
                        }
                        close_pipes++;
                    }
                    free(p->pipes);
                }
            } else {
                do_exit = 1;
            }
        } else if (!do_exit) {
            hanged_pids_end--;
        }
    }
    if (!do_exit) {
        hanged_pids_end = hanged_pids;
    }
}
char *CURRENT_DIR;
int builtin(Command command) {
    if (strcmp(*command, "exit") == 0) {
        handle_hanged();
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
        chdir(new_dir);
        new_dir = get_current_dir_name();
        int len = strlen(new_dir);
        if (strlen(CURRENT_DIR) < len) {
            free(CURRENT_DIR);
            CURRENT_DIR = malloc(len + 1);
            if (CURRENT_DIR == NULL) {
                perror("malloc");
                exit(1);
            }
        }
        strcpy(CURRENT_DIR, new_dir);
        return 0;
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
int run_commands(CommandReturn commandret, char **str, int last, int to_close) {
    Command *commands = commandret.command;
    if (commands == NULL) {
        printf("ERROR\n");
        return 1;
    }
    char *file = commandret.file;
    char *file_input = commandret.file_input;
    int *close_pipes = commandret.close_pipes;
    char forget = commandret.forget;
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
    int *pipes;
    int *pids;
    if (!forget) {
        pipes = malloc(sizeof(int) * count);
        if (pipes == 0) {
            perror("malloc");
            exit(1);
        }
        pids = malloc(sizeof(int) * count);
        if (pids == 0) {
            perror("malloc");
            exit(1);
        }
    }
    char no_stdinout = last == -1;
    int file_pipe = -1;
    if (file_input != NULL) {
        last = open(file_input, O_CREAT | O_RDONLY, 0644);
        if (last == -1) {
            perror("open");
            exit(1);
        }
        file_pipe = last;
    }
    int p[2];
    int pid = 0;
    while (*commands != NULL) {
        char use_stdout = commands[1] == NULL && str == NULL && !no_stdinout;
        if (!use_stdout && pipe(p) == -1) {
            perror("pipe");
            exit(1);
        }
        pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            if (to_close != -1) {
                if (close(to_close) == -1) {
                    perror("close");
                    exit(1);
                }
                to_close = -1;
            }
            if (use_stdout) {
                if (file != NULL) {
                    p[1] = open(file, O_CREAT | O_WRONLY, 0644);
                    if (p[1] == -1) {
                        perror("open");
                        exit(1);
                    }
                } else {
                    p[1] = STDOUT_FILENO;
                }
            } else {
                if (close(p[0]) == -1) {
                    perror("close");
                    exit(1);
                }
            }
            run_command(*commands, last, p[1]);
        }
        if (!use_stdout) {
            if (close(p[1]) == -1) {
                perror("close");
                exit(1);
            }
            last = p[0];
        } else {
            last = -1;
        }
        if (forget) {
            PidClosePipes pidpipes;
            pidpipes.pid = pid;
            pidpipes.pipes = malloc(3 * sizeof(int));
            if (last == -1) {
                if (file_pipe != -1) {
                    pidpipes.pipes[0] = file_pipe;
                    pidpipes.pipes[1] = -1;
                } else {
                    pidpipes.pipes[0] = -1;
                }
            } else {
                if (file_pipe != -1) {
                    pidpipes.pipes[0] = last;
                    pidpipes.pipes[1] = file_pipe;
                    pidpipes.pipes[2] = -1;
                } else {
                    pidpipes.pipes[0] = last;
                    pidpipes.pipes[1] = -1;
                }
            }
            *hanged_pids_end = pidpipes;
            hanged_pids_end++;
        } else {
            *pipes = last;
            pipes++;
            *pids = pid;
            pids++;
        }
        commands++;
    }
    if (!forget) {
        pipes -= count;
        pids -= count;
    }
    if (str != NULL) {
        forget = 0;
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
    if (forget) {
        int i = 0;
        while (close_pipes[i] != -1) {
            i++;
        }
        if (i != 0) {
            int j = 0;
            int *last = (hanged_pids_end - 1)->pipes;
            while (last[j] != -1) {
                j++;
            }
            last = realloc(last, (i + j + 1) * sizeof(int));
            if (last == NULL) {
                perror("realloc");
                exit(1);
            }
            for (int k = 0; k < i; k++) {
                last[j + k] = close_pipes[k];
            }
            last[i + j] = -1;
            (hanged_pids_end - 1)->pipes = last;
        }
        return 0;
    }
    int status;
    for (int i = 0; i < count; i++) {
        if (SIG_INT && kill(pids[i], 9) == -1) {
            perror("kill");
            exit(1);
        }
        if (waitpid(pids[i], &status, 0) == -1) {
            if (SIG_INT) {
                if (kill(pids[i], 9) == -1) {
                    perror("kill");
                    exit(1);
                }
                if (waitpid(pids[i], &status, 0) == -1) {
                    perror("waitpid");
                    exit(1);
                }
            } else {
                perror("waitpid");
                exit(1);
            }
        }
        if (i == 0 && file_pipe != -1) {
            if (close(file_pipe) == -1) {
                perror("close");
                exit(1);
            }
        }
        if (pipes[i] != -1) {
            if (close(pipes[i]) == -1) {
                perror("close");
                exit(1);
            }
        }
    }
    while (*close_pipes != -1) {
        if (close(*close_pipes) == -1) {
            perror("close");
            exit(1);
        }
        close_pipes++;
    }
    free(pids);
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
    ret.close_pipes = malloc(sizeof(int) * (strlen(line) + 2));
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
            run_commands(c, NULL, p[0], p[1]);
            strcat(buf, "/dev/fd/");
            sprintf(str, "%d", p[1]);
            strcat(buf, str);
            int *close_pipes = ret.close_pipes;
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
        free_commands_ret(ret);
        ret.command = NULL;
        return ret;
    }
    return ret;
}
void handler(int signo, siginfo_t *info, void *context) { SIG_INT = 1; }
int main() {
    struct sigaction act = {0};
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = &handler;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        return 1;
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
            perror("malloc");
            return 1;
        }
        snprintf(prompt, size, "%s %d ", CURRENT_DIR, ret);
        char *line = readline(prompt);
        free(prompt);
        if (line == NULL) {
            return 0;
        }
        if (strlen(line) != 0) {
            add_history(line);
        }
        CommandReturn commands = get_commands(line, 0);
        free(line);
        ret = run_commands(commands, NULL, STDIN_FILENO, -1);
        free_commands_ret(commands);
        handle_hanged();
    }
}
