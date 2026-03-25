#ifndef PARSE_H
#define PARSE_H
typedef char *Arg;
typedef Arg *Command;
typedef struct CommandReturn {
    Command *command;
    char *file;
    char *file_input;
    char forget;
    int **close_pipes;
    size_t length;
} CommandReturn;
void free_commands_ret(CommandReturn commands);
CommandReturn get_commands(char *, char);
#endif