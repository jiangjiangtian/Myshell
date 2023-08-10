#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#include "built_in_command.h"

#define MAXLEN 128
#define MAXARGS 16
#define MAXJOBS 32
#define FILELEN 16
#define MODE (S_IRUSR | S_IWUSR | S_IXUSR | S_IROTH | S_IWOTH | S_IXOTH | S_IRGRP | S_IWGRP | S_IXGRP)

mode_t mode; // 创建文件时的权限

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
    char cmdline[MAXLEN];
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
    int mode;   // 追加或截断
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
    struct cmd command;
    char cmdline[MAXLEN];   // 由于在解析中，我们会修改原始的命令，所以我们需要另一个字符数组
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

struct cmd *parseredir(char *buf, struct cmd *inner_command);
struct cmd *parseexec(char *buf);
struct cmd *parsepipe(char *buf);
struct cmd *parsecmd(char *cmd);
void eval(struct cmd *command);
int is_built_in_command(int argc, char *argv[]);
void test_parse(struct cmd *command);

int main() {
    static char cmd[MAXLEN];
    // 通过 getenv 函数获得 PWD 环境变量
    // PWD 的值为启动时的工作目录
    strcpy(pwd, getenv("PWD"));
    // 设置 shell 环境变量
    setenv("SHELL", pwd, 1);
    mode = umask(0);  // 获得默认的设置
    umask(mode);      // 恢复默认设置
    while (1) {
        print_prompt();
        readcmd(cmd);
        struct cmd *command = parsecmd(cmd);
        eval(command);
    }

    return 0;
}

/**
 * create_pipecmd - 创建一个 pipecmd 对象
 */
struct cmd *create_pipecmd(struct cmd *left, struct cmd *right) {
    struct pipecmd *pipe_cmd = (struct pipecmd *)malloc(sizeof(struct pipecmd));
    pipe_cmd->type = PIPE;
    pipe_cmd->fgbg = 0;
    pipe_cmd->type = PIPE;
    pipe_cmd->left = left;
    pipe_cmd->right = right;
    return (struct cmd *)pipe_cmd;
}

/**
 * create_execcmd - 创建一个 execcmd 对象
 */
struct cmd *create_execcmd(char *buf) {
    struct execcmd *exec_cmd = (struct execcmd *)malloc(sizeof(struct execcmd));
    exec_cmd->type = EXEC;
    exec_cmd->fgbg = 0;
    strcpy(exec_cmd->cmdline, buf);
    return (struct cmd *)exec_cmd;
}

/**
 * create_redircmd - 创建一个 redircmd 对象
 */
struct cmd *create_redircmd(struct cmd *inner_command, int mode, char *in_file, char *out_file) {
    struct redircmd *redir_cmd = (struct redircmd *)malloc(sizeof(struct redircmd));
    redir_cmd->type = REDIR;
    redir_cmd->fgbg = 0;
    redir_cmd->command = inner_command;
    redir_cmd->mode = mode;
    strcpy(redir_cmd->in_file, in_file);
    strcpy(redir_cmd->out_file, out_file);
    return (struct cmd *)redir_cmd;
}

/*
 * parsecmd - 解析输入的命令，将输入的命令按照空格
 * 进行分割，然后存储到 argv 数组中
 * 返回值：1——后台运行，0——前台运行
 */
struct cmd *parsecmd(char *cmd) {
    struct cmd *command = parsepipe(cmd);
    return command;
}

/**
 * next_empty - 返回下一个字符为空字符或空格的位置
 */
char *next_empty(char *pos) {
    while (*pos != '\0' && *pos != ' ') {
        pos++;
    }
    return pos;
}
/**
 * parseredir - 解析是否有重定向，如果有，则创建一个 redircmd 对象
 */
struct cmd *parseredir(char *buf, struct cmd *inner_command) {
    char *pos;
    char in_file[FILELEN] = { '\0' };
    char out_file[FILELEN] = { '\0' };
    int mode = 0;
    struct cmd *command = inner_command;
    if ((pos = strchr(buf, '>')) != NULL) {
        if (*(pos + 1) == '>') {    // 追加
            mode = O_APPEND | O_CREAT | O_WRONLY;
            pos++;
        } else {   // 截断
            mode = O_TRUNC | O_CREAT | O_WRONLY;
        }
        // 复制输出文件名
        pos = next_nonempty(pos + 1);
        char *end_pos = next_empty(pos + 1);
        strncpy(out_file, pos, end_pos - pos);
    }

    if ((pos = strchr(buf, '<')) != NULL) {
        // 复制输入文件名
        pos = next_nonempty(pos + 1);
        char *end_pos = next_empty(pos + 1);
        strncpy(in_file, pos, end_pos - pos);
    }

    if (*in_file || *out_file) {    // 存在重定向
        command = create_redircmd(inner_command, mode, in_file, out_file);
    }

    return command;
}

/**
 * parseecex - 解析命令，将命令行参数存储到结构体中
 */
struct cmd *parseexec(char *buf) {
    int argc = 0;
    int i = 0;
    int begin = 0;
    int len = strlen(buf);
    int bg = 0;
    struct cmd *command = create_execcmd(buf);
    struct execcmd *ret = (struct execcmd *)command;
    command = parseredir(buf, command);
    char *array = ret->cmdline; // 指向结构体内部的命令
    while (i < len) {
        // 找到下一个非空字符，作为下一个参数的开始位置
        while (array[begin] == ' ' || array[begin] == '\0') {
            ++begin;
        }
        if (array[begin] == '>' || array[begin] == '<') {   // 如果遇到重定向符号，则直接跳过
            if (array[begin + 1] == '>') {
                begin++;
            }
            char *pos = next_nonempty(array + begin + 1);
            char *end_pos = next_empty(pos + 1);
            begin = i = end_pos - array;
            continue;
        }
        // 找到下一个空字符，作为当前参数的结束位置，然后将
        // 该位置的字符设置为 '\0'
        i = begin + 1;
        while (array[i] != ' ' && array[i] != '\0') {
            ++i;
        }
        array[i] = '\0';
        ret->argv[argc++] = &array[begin];
        begin = ++i;
    }
    // 判断为前台运行还是后台运行
    if (strcmp(ret->argv[argc - 1], "&") == 0) {
        --argc;
        ret->argv[argc] = NULL;
        ret->fgbg = 1;
    }
    ret->argv[argc] = NULL;
    ret->argc = argc;
    return command;
}

/**
 * parsepipe - 解析是否为管道，如果发现 |，
 * 则说明有管道，如果没有，则调用 parseexec 解析命令
 */
struct cmd *parsepipe(char *buf) {
    struct cmd *command = NULL;
    char *pos = strchr(buf, '|');

    if (pos != NULL) { // 找到 '|'，为管道
        *pos = '\0';
        char *next = next_nonempty(pos + 1);
        command = parseexec(buf);
        command = create_pipecmd(command, parsecmd(next));
    } else {
        command = parseexec(buf);
    }
    return command;
}

/**
 * eval - 根据传入的 command 的类型选择运行的方式
 */
void eval(struct cmd *command) {
    struct execcmd *exec_cmd;
    struct pipecmd *pipe_cmd;
    struct redircmd *redir_cmd;
    int built_in;
    int fd;
    int fds[2];
    pid_t pid = 0;

    if (command->fgbg) {
        if (fork() == 0) {
            eval(command);
        }
    }

    switch (command->type) {
        case EXEC:  // 直接运行
            exec_cmd = (struct execcmd *)command;
            built_in = is_built_in_command(exec_cmd->argc, exec_cmd->argv); // 判断是否是内部命令
            if (!built_in && (pid = fork()) == 0) {    // 不为内置命令
                execve(exec_cmd->argv[0], exec_cmd->argv, __environ);
                fprintf(stderr, "execve error!\n");
            } else if (pid != 0) {  // 等待子进程运行结束
                wait(0);
            }
            break;
        case PIPE:  // 管道
            pipe_cmd = (struct pipecmd *)command;
            if (fork() == 0) {
                if (pipe(fds) == -1) {
                    fprintf(stderr, "execve error: %s\n", strerror(errno));
                }

                if (fork() == 0) {  //  right
                    close(0);
                    dup(fds[0]);
                    close(fds[0]);
                    close(fds[1]);
                    eval(pipe_cmd->right);
                }

                if (fork() == 0) {    //  left
                    close(1);
                    dup(fds[1]);
                    close(fds[1]);
                    close(fds[0]);
                    close(fds[1]);
                    eval(pipe_cmd->left);
                }
                close(fds[0]);
                close(fds[1]);
                // 等待两个子进程完成运行
                wait(0);
                wait(0);
            }
            break;
        case REDIR:
            redir_cmd = (struct redircmd *)command;
            if (fork() == 0) {
                if (*redir_cmd->in_file) {  // 重定向标准输入
                    close(0);
                    if ((fd = open(redir_cmd->in_file, O_RDONLY)) != 0) {
                        fprintf(stderr, "open error: %s\n", strerror(errno));
                    }
                }
                if (*redir_cmd->out_file) { // 重定向标准输出
                    close(1);                   
                    if ((fd = open(redir_cmd->out_file, redir_cmd->mode, MODE ^ mode)) != 1) {
                        fprintf(stderr, "open error: %s\n", strerror(errno));
                    }
                }
                exec_cmd = (struct execcmd *)redir_cmd->command;
                built_in = is_built_in_command(exec_cmd->argc, exec_cmd->argv); // 判断是否是内部命令
                if (!built_in) {
                    execve(exec_cmd->argv[0], exec_cmd->argv, __environ);
                    fprintf(stderr, "execve error: %s\n", strerror(errno));
                } else {
                    exit(0);
                }
            }
            wait(0);
            break;
        default:
            fprintf(stderr, "unknown command type\n");
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
        umask_imp(argv);
        return 15;
    }
    return 0;
}

/**
 * exec_imp - exec 内部命令的实现
 */
void exec_imp(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        argv[i] = argv[i + 1];
    }
    int built_in = is_built_in_command(argc, argv);

    if (!built_in) {

    }
}

/**
 * test_parse - 测试解析的结果
 */
void test_parse(struct cmd *command) {
    struct execcmd *exec_cmd;
    struct pipecmd *pipe_cmd;
    struct redircmd *redir_cmd;
    printf("%s\n", command->fgbg ? "bg" : "fg");
    switch (command->type) {
        case EXEC:
            exec_cmd = (struct execcmd *)command;
            printf("exec:\n");
            char *str = exec_cmd->argv[0];
            for (int i = 0; str; i++, str = exec_cmd->argv[i]) {
                printf("%s\n", str);
            }
            break;
        case PIPE:
            pipe_cmd = (struct pipecmd *)command;
            printf("pipe:\n");
            printf("left:\n");
            test_parse(pipe_cmd->left);
            printf("right:\n");
            test_parse(pipe_cmd->right);
            break;
        case REDIR:
            redir_cmd = (struct redircmd *)command;
            printf("redir:\n");
            printf("in_file: %s\n", redir_cmd->in_file);
            printf("out_file: %s\n", redir_cmd->out_file);
            break;
    }
}