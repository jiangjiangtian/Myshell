#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "built_in_command.h"

#define MAXLEN 512
#define MAXARGS 16
#define MAXJOBS 32
#define FILELEN 16

/**
 * 三种命令状态，EXEC 为正常运行，PIPE 为管道，REDIR 为重定向
 */
enum cmd_type { EXEC, PIPE, REDIR };

/**
 * 表示命令的结构体，模拟 C++ 的继承
 */
struct cmd {
    enum cmd_type type;
    int fgbg;
};

/**
 * 直接运行的命令的结构体，type 一定为 EXEC
 */
struct execcmd {
    enum cmd_type type;
    int fgbg;
    int argc;
    char *argv[MAXARGS];
};

/**
 * 管道命令的结构体，type 一定为 PIPE
 */
struct pipecmd {
    enum cmd_type type;
    int fgbg;
    struct cmd *left;
    struct cmd *right;
};

struct redircmd {
    enum cmd_type type;
    int fgbg;
    struct cmd *command;
    char in_file[FILELEN];
    char out_file[FILELEN];
};

/**
 * 工作的状态
 */
enum job_state { INVALID, BG, FG, ST };

/**
 * 表示工作的结构体
 */
struct job_t {
    int jid;
    pid_t pid;
    enum job_state state;
    char cmdline[MAXLEN];
    struct cmd command;
} jobs[MAXJOBS];

int nextjid = 0;

char pwd[MAXLEN];   // 表示当前工作目录
/**
 * print_prompt - 输出提示符
 */
void print_prompt() {
    printf("%s$ ", pwd);
}

/**
 * readcmd - 从标准输入读入命令，当遇到换行符时，停止读入
 */
void readcmd(char *cmd) {
    char ch;
    int len = 0;
    while ((ch = getchar()) != '\n') {
        cmd[len++] = ch;
    }
    cmd[len] = '\0';
}

char whitespace[] = " \t";

/**
 * next_nonempty - 寻找下一个非空白字符
 */
char *next_nonempty(char *buf) {
    while (*buf != '\0' && strchr(whitespace, *buf)) {
        buf++;
    }
    return buf;
}

struct cmd *parsecmd(char *cmd);
void eval(char *buf);
int is_built_in_command(int argc, char *argv[]);

int main() {
    char cmd[MAXLEN];
    // 通过 getenv 函数获得 PWD 环境变量
    // PWD 的值为启动时的工作目录
    strcpy(pwd, getenv("PWD"));
    // 设置 shell 环境变量
    setenv("SHELL", pwd, 1);
    while (1) {
        print_prompt();
        readcmd(cmd);
        eval(cmd);
    }

    return 0;
}

struct cmd *create_pipecmd(struct cmd *left, struct cmd *right) {
    struct pipecmd *pipe_cmd = (struct pipecmd *)malloc(sizeof(struct pipecmd));
    pipe_cmd->type = PIPE;
    pipe_cmd->left = left;
    pipe_cmd->right = right;
    return (struct cmd *)pipe_cmd;
}

struct cmd *create_execcmd(int argc, char *argv[], int fgbg) {
    struct execcmd *exec_cmd = (struct execcmd *)malloc(sizeof(struct execcmd));
    exec_cmd->type = EXEC;
    exec_cmd->argc = argc;
    for (int i = 0; i <= argc; i++) {
        exec_cmd->argv[i] = argv[i];
    }
    exec_cmd->fgbg = fgbg;
    return exec_cmd;
}

/*
 * parsecmd - 解析输入的命令，将输入的命令按照空格
 * 进行分割，然后存储到 argv 数组中
 * 返回值：1——后台运行，0——前台运行
 */
struct cmd *parsecmd(char *cmd) {
    static char buf[MAXLEN];
    int i = 0;
    int begin = 0;
    int len = strlen(buf);
    strcpy(buf, cmd);
    
    /*while (i < len) {
        // 找到下一个非空字符，作为下一个参数的开始位置
        while (buf[begin] == ' ' || buf[begin] == '\0') {
            ++begin;
        }
        // 找到下一个空字符，作为当前参数的结束位置，然后将
        // 该位置的字符设置为 '\0'
        i = begin + 1;
        while (buf[i] != ' ' && buf[i] != '\0') {
            ++i;
        }
        buf[i] = '\0';
        argv[(*argc)++] = &buf[begin];
        begin = ++i;
    }
    
    // 判断为前台运行还是后台运行
    if (strcmp(argv[*argc - 1], "&") == 0) {
        --(*argc);
        argv[*argc] = NULL;
        return 1;
    }
    argv[*argc] = NULL;*/
    return 0;
}

struct cmd *parseexec(char *buf) {
    int argc = 0;
    char *argv[MAXARGS];
    int i = 0;
    int begin = 0;
    int len = strlen(buf);
    int bg = 0;
    while (i < len) {
        // 找到下一个非空字符，作为下一个参数的开始位置
        while (buf[begin] == ' ' || buf[begin] == '\0') {
            ++begin;
        }
        // 找到下一个空字符，作为当前参数的结束位置，然后将
        // 该位置的字符设置为 '\0'
        i = begin + 1;
        while (buf[i] != ' ' && buf[i] != '\0') {
            ++i;
        }
        buf[i] = '\0';
        argv[argc++] = &buf[begin];
        begin = ++i;
    }
    // 判断为前台运行还是后台运行
    if (strcmp(argv[argc - 1], "&") == 0) {
        --argc;
        argv[argc] = NULL;
        bg = 1;
    }
    argv[argc] = NULL;
    struct cmd *command = create_execcmd(argc, argv, bg);
    return command;
}

/**
 * parsepipe - 解析是否为管道
 */
struct cmd *parsepipe(char *buf) {
    struct cmd *command;
    char *pos = strchr(buf, '&');

    if (pos != NULL) { // 找到 '&'，为管道
        *pos = '\0';
        char *next = next_nonempty(pos + 1);
        command = parseexec(buf);
        command = create_pipecmd(command, parsecmd(next));
    } else {
        command = parseexec(buf);
    }
    return command;
}

void eval(char *buf) {
    // 空行，直接退出
    if (*buf == '\0') {
        return;
    }

    char *argv[MAXARGS];
    int argc = 0;
    parsecmd(buf);

    // 输入的为空命令，直接退出
    if (argc == 0) {
        return;
    }
    
    int built_in = is_built_in_command(argc, argv);
    
    if (!built_in) {

    }
}

/**
 * is_buiit_in_command - 判断是否为内部命令，若是内部命令，
 * 则运行内部命令，并返回非零值；否则，返回 0，表示当前命令不为
 * 内部命令
 */
int is_built_in_command(int argc, char *argv[]) {
    if (strcmp(argv[0], "bg") == 0) {
        
        return 1;
    } else if (strcmp(argv[0], "cd") == 0) {
        cd_imp(argv[1]);
        return 2;
    } else if (strcmp(argv[0], "clr") == 0) {
        clr_imp();
        return 3;
    } else if (strcmp(argv[0], "dir") == 0) {
        dir_imp(argv[1]);
        return 4;
    } else if (strcmp(argv[0], "echo") == 0) {
        echo_imp(argv);
        return 5;
    } else if (strcmp(argv[0], "exec") == 0) {
        exec_imp(argc, argv);
        exit(0);
    } else if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(argv[0], "fg") == 0) {

    } else if (strcmp(argv[0], "help") == 0) {
        help_imp();
        return 9;
    } else if (strcmp(argv[0], "jobs") == 0) {

    } else if (strcmp(argv[0], "pwd") == 0) {
        printf("%s\n", pwd);
        return 11;
    } else if (strcmp(argv[0], "set") == 0) {
        set_imp();
        return 12;
    } else if (strcmp(argv[0], "test") == 0) {

    } else if (strcmp(argv[0], "time") == 0) {
        time_imp();
        return 14;
    } else if (strcmp(argv[0], "umask") == 0) {

    }
    return 0;
}

void exec_imp(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        argv[i] = argv[i + 1];
    }
    int built_in = is_built_in_command(argc, argv);

    if (!built_in) {

    }
}

