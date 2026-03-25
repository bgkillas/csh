#ifndef RUN_H
#define RUN_H
typedef struct PidClosePipes {
    int pid;
    int *pipes;
} PidClosePipes;
extern PidClosePipes hanged_pids[BUF_SIZE];
extern PidClosePipes *hanged_pids_end;
void handle_hanged();
int run_commands(CommandReturn, char **, int, int);
#endif