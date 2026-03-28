#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "main.h"
#include "parse.h"
#include "run.h"
void run_command(Command command, int input, int output) {
    if (input != STDIN_FILENO) {
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
            int n = waitpid(p->pid, &status, WNOHANG);
            if (n == -1) {
                perror("waitpid");
                exit(1);
            }
            if (n == p->pid && WIFEXITED(status)) {
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
}
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
        size_t len = strlen(new_dir);
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
    int **close_pipes = commandret.close_pipes;
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
    int *pipes = NULL;
    int *pids = NULL;
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
    int pi[2];
    int pid = 0;
    int **close_pipe = commandret.close_pipes;
    while (*commands != NULL) {
        char use_stdout = commands[1] == NULL && str == NULL && !no_stdinout;
        if (!use_stdout && pipe(p) == -1) {
            perror("pipe");
            exit(1);
        }
        if (last == -1 && pipe(pi) == -1) {
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
                    p[1] = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
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
            if (last == -1) {
                if (close(pi[1]) == -1) {
                    perror("close");
                    exit(1);
                }
                last = pi[0];
            }
            run_command(*commands, last, p[1]);
        }
        if (last == -1) {
            if (close(pi[0]) == -1) {
                perror("close");
                exit(1);
            }
            if (close(pi[1]) == -1) {
                perror("close");
                exit(1);
            }
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
            int i = 0;
            if (*close_pipe != NULL) {
                while ((*close_pipe)[i] != -1) {
                    i++;
                }
            }
            pidpipes.pipes = malloc((3 + i) * sizeof(int));
            int j = 0;
            if (last != -1) {
                pidpipes.pipes[j] = last;
                j++;
            }
            if (file_pipe != -1) {
                pidpipes.pipes[j] = file_pipe;
                file_pipe = -1;
                j++;
            }
            for (int k = 0; k < i; k++) {
                pidpipes.pipes[j + k] = (*close_pipe)[k];
            }
            pidpipes.pipes[i + j] = -1;
            *hanged_pids_end = pidpipes;
            hanged_pids_end++;
        } else {
            *pipes = last;
            pipes++;
            *pids = pid;
            pids++;
        }
        commands++;
        close_pipe++;
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
        return 0;
    }
    pipes -= count;
    pids -= count;
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
        if (file_pipe != -1) {
            if (close(file_pipe) == -1) {
                perror("close");
                exit(1);
            }
            file_pipe = -1;
        }
        if (pipes[i] != -1) {
            if (close(pipes[i]) == -1) {
                perror("close");
                exit(1);
            }
        }
        if (close_pipes[i] != NULL) {
            int *close_pip = close_pipes[i];
            while (*close_pip != -1) {
                if (close(*close_pip) == -1) {
                    perror("close");
                    exit(1);
                }
                close_pip++;
            }
        }
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
