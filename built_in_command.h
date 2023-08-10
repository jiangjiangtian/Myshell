#ifndef __BUILT_IN_COMMAND_H_
#define __BUILD_IN_COMMAND_H_

void cd_imp(const char *dir_path);
void dir_imp(char *dir_path);
void echo_imp(char *argv[]);
void exec_imp(int argc, char *argv[]);
void clr_imp(void);
void time_imp();
void help_imp(void);
void set_imp(void);
void umask_imp(char *argv[]);

#endif