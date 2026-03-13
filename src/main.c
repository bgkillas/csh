#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
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
    int i = 0;
    commands[i] = malloc(sizeof(Command) * (strlen(line) / 2 + 2));
    int j = 0;
    commands[i][j] = malloc(strlen(line) + 1);
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
                commands[i][j] = malloc(strlen(line) + 1);
                break;
            case ' ':
                if (last != ' ' && last != '|' && last != '\0') {
                    commands[i][j][k] = '\0';
                    k = 0;
                    j++;
                    commands[i][j] = malloc(strlen(line) + 1);
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
    commands[i][j][k] = '\0';
    commands[i][j + 1] = NULL;
    commands[i + 1] = NULL;
    if (state != NONE || last == '|') {
        free_commands(commands);
        return NULL;
    }
    return commands;
}
int run_commands(Command *commands) {
    int i = 0;
    while (commands[i] != NULL) {
        int j = 0;
        while (commands[i][j] != NULL) {
            printf("\"%s\" ", commands[i][j]);
            j++;
        }
        printf("\n");
        i++;
    }
    // TODO
    return 0;
}
int main() {
    int ret = 0;
    using_history();
    while (true) {
        char *dir = get_current_dir_name();
        int size = snprintf(NULL, 0, "%s %d ", dir, ret) + 1;
        char *prompt = malloc(size);
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
    return 0;
}
