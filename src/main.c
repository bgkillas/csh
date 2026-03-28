#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "main.h"
#include "parse.h"
#include "run.h"
char *CURRENT_DIR;
PidClosePipes hanged_pids[BUF_SIZE];
PidClosePipes *hanged_pids_end = hanged_pids;
volatile int SIG_INT = 0;
void handler(int signo, siginfo_t *info, void *context) {
    (void)signo;
    (void)info;
    (void)context;
    SIG_INT = 1;
}
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
        int stdin = STDIN_FILENO;
        if (commands.forget) {
            stdin = -1;
        }
        ret = run_commands(commands, NULL, stdin, -1);
        free_commands_ret(commands);
        handle_hanged();
    }
}
