#ifndef MAIN_H
#define MAIN_H
char *get_current_dir_name();
extern char *CURRENT_DIR;
#define BUF_SIZE 65536
extern volatile int SIG_INT;
#endif