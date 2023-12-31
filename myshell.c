#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
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

/**
 * 管理重定向命令的结构体，type 一定为 REDIR
 */
struct redircmd {
    enum cmd_type type;
    int fgbg;
    struct cmd *command;
    int mode;   // 追加或截断
    char in_file[FILELEN];
    char out_file[FILELEN];
};

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * 最多一个作业能在 FG 状态
 */
/**
 * 作业的状态
 */
enum job_state { INVALID, BG, FG, ST };

/**
 * 表示作业的结构体
 */
struct job_t {
    int jid;
    pid_t pid;
    enum job_state state;
    struct cmd *command;
    char cmdline[MAXLEN];   // 由于在解析中，我们会修改原始的命令，所以我们需要另一个字符数组
} jobs[MAXJOBS];

int nextjid = 1;    // 下一个要分配的 job id
sig_atomic_t fgpid = 0; // 当我们从后台将一个作业移至前台，设置 fgpid, fgpid 为原子性变量
char pwd[MAXLEN];   // 表示当前作业目录

struct cmd *parseredir(char *buf, struct cmd *inner_command);
struct cmd *parseexec(char *buf);
struct cmd *parsepipe(char *buf);
struct cmd *parsecmd(char *cmd);
void eval(char *cmdline, struct cmd *command);
int is_built_in_command(struct cmd *command);
struct cmd *create_pipecmd(struct cmd *left, struct cmd *right);
struct cmd *create_execcmd(char *buf);
struct cmd *create_redircmd(struct cmd *inner_command, int mode, char *in_file, char *out_file);
void execredir(struct redircmd *redir_cmd);
struct execcmd *getexeccmd(struct cmd *command);
void test_parse(struct cmd *command);

/*******************
 * 作业相关函数
*******************/
void initjob();
struct job_t *addjob(char *cmdline, int bgfg, struct cmd *command, pid_t pid);
void listjobs();
int maxjid();
int deljob(pid_t pid);
struct job_t *getjobjid(int jid);
struct job_t *getjobpid(pid_t pid);

/*******************
 * 信号相关函数
*******************/
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

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
    char ch = '\0';
    int len = 0;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        cmd[len++] = ch;
    }
    if (len == 0 && ch == EOF) { // 到达文件末尾
        exit(0);
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

int main(int argc, char *argv[]) {
    static char cmdline[MAXLEN];
    // 通过 getenv 函数获得 PWD 环境变量
    // PWD 的值为启动时的作业目录
    strcpy(pwd, getenv("PWD"));
    // 设置 shell 环境变量
    setenv("SHELL", pwd, 1);
    mode = umask(0);  // 获得默认的设置
    umask(mode);      // 恢复默认设置
    Signal(SIGCHLD, sigchld_handler);  // 设置子进程终止时调用的函数
    Signal(SIGTSTP, sigtstp_handler);  // 设置子进程暂停时调用的函数, ctrl + z
    Signal(SIGINT, sigint_handler);    // ctrl + c
    initjob();
    pid_t pid;
    sigset_t oldmask, mask;
    int read_file = 0;  // 是否从文件中读入命令
    int fd;             // 若从文件中读入命令，则 fd 保存打开的文件描述符
    if (argc >= 2) {    // 从命令行传入文件，即从命令行读入命令
        read_file = 1;
        if ((fd = open(argv[1], O_RDONLY)) < 0) {   // 打开文件
            fprintf(stderr, "open %s error: %s\n", argv[1], strerror(errno));
            exit(1);
        }
        // 打开文件成功，将其重定向到标准输入上
        close(0);   // 关闭标准输入
        dup(fd);    // 重定向
        close(fd);
    }
    while (1) {
        if (!read_file) {   // 从标准输入读入
            print_prompt();
        }
        readcmd(cmdline);
        struct cmd *command = parsecmd(cmdline);
        if (!command) { // 空命令
            continue;
        }
        if (!command->fgbg && (command->type == EXEC || strstr(cmdline, "exec")) &&
            is_built_in_command(command) != 0) {
                continue;   // 内部命令且为前台运行
        }
        // 阻塞 SIGCHLD 信号，防止子进程在父进程调用 addjob
        // 之前就已经调用 deljob
        sigfillset(&mask);
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        if ((pid = fork()) == 0) {
            sigprocmask(SIG_SETMASK, &oldmask, NULL);
            setpgid(0, 0);
            eval(cmdline, command);
        } else {
            if (!command->fgbg) {
                // 阻塞所有的信号，保护 jobs 数组
                fgpid = pid;
                addjob(cmdline, command->fgbg, command, pid);
                sigprocmask(SIG_SETMASK, &oldmask, NULL);
                waitfg();
            }
        }
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

/**
 * free_cmd - 释放 struct cmd 的资源
 */
void free_cmd(struct cmd *command) {
    struct execcmd *exec_cmd;
    struct pipecmd *pipe_cmd;
    struct redircmd *redir_cmd;
    // 判断 command 的类型
    switch (command->type) {
        case EXEC:
            free(command);
            break;
        case PIPE:
            pipe_cmd = (struct pipecmd *)command;
            free_cmd(pipe_cmd->left);   // 释放左子结点的资源
            free_cmd(pipe_cmd->right);  // 释放右子结点的资源
            free(command);
            break;
        case REDIR:
            redir_cmd = (struct redircmd *)command;
            free_cmd(redir_cmd->command);  // 释放内部命令的资源
            free(command);
            break;
    }
}
/*
 * parsecmd - 解析输入的命令，将输入的命令按照空格
 * 进行分割，然后存储到 argv 数组中
 * 返回值：1——后台运行，0——前台运行
 */
struct cmd *parsecmd(char *cmd) {
    if (*cmd == 0) {    // 空命令，返回 NULL
        return NULL;
    }
    struct cmd *command = parsepipe(cmd);
    if (strchr(cmd, '&') != NULL) { // 判断为前台运行还是后台运行
        command->fgbg = 1;
    }
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
        if (strcmp(&array[begin], "&") == 0) {
            // 我们只假设 & 为命令的最后一个参数，所以当我们遇到 & 时，
            // 我们直接退出，尽管后面还可能有其他的参数
            break;
        }
        ret->argv[argc++] = &array[begin];
        begin = ++i;
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
        *pos = '|';  // 为了能够输出整条命令，我们将之前清空的 | 恢复
    } else {
        command = parseexec(buf);
    }
    return command;
}

/**
 * execredir - 执行重定向命令
 */
void execredir(struct redircmd *redir_cmd) {
    int fd;
    struct execcmd *exec_cmd;
    int built_in;
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
    built_in = is_built_in_command(redir_cmd->command); // 判断是否是内部命令
    if (!built_in) {
        execve(exec_cmd->argv[0], exec_cmd->argv, __environ);
        fprintf(stderr, "%s: 未找到命令\n", exec_cmd->argv[0]);
        exit(1);
    } else {
        exit(0);
    }
}

/**
 * eval - 根据传入的 command 的类型选择运行的方式
 */
void eval(char *cmdline, struct cmd *command) {
    struct execcmd *exec_cmd;
    struct pipecmd *pipe_cmd;
    int built_in;
    int fd;
    int fds[2];
    pid_t pid = 0;

    if (command->fgbg) {  // 后台运行，直接创建一个进程运行
        sigset_t mask, oldmask;
        sigfillset(&mask);
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        if ((pid = fork()) == 0) {
            sigprocmask(SIG_SETMASK, &oldmask, NULL);
            command->fgbg = 0;
            setpgid(0, 0);
            eval(cmdline, command);
        } else {
            // 阻塞所有的信号，保护 jobs 数组
            struct job_t *job = addjob(cmdline, command->fgbg, command, pid);
            printf("[%d] (%d) %s\n", job->jid, job->pid, job->cmdline);
            sigprocmask(SIG_SETMASK, &oldmask, NULL);
            exit(0);
        }
    }

    switch (command->type) {
        case EXEC:  // 直接运行
            exec_cmd = (struct execcmd *)command;
            built_in = is_built_in_command(command); // 判断是否是内部命令
            if (!built_in) {    // 不为内置命令
                execve(exec_cmd->argv[0], exec_cmd->argv, __environ);
                fprintf(stderr, "%s: 未找到命令\n", cmdline);
                exit(1);
            } else {    // 内部命令，直接退出
                exit(0);
            }
            break;
        case PIPE:  // 管道
            pipe_cmd = (struct pipecmd *)command;
            if (pipe(fds) == -1) {
                fprintf(stderr, "pipe error: %s\n", strerror(errno));
            }

            if (fork() == 0) {  //  right
                close(0);
                dup(fds[0]);    // 将管道复制到标准输入上
                close(fds[0]);
                close(fds[1]);
                eval(cmdline, pipe_cmd->right);
            }

            if (fork() == 0) {    //  left
                close(1);
                dup(fds[1]);      // 将管道复制到标准输出上
                close(fds[0]);
                close(fds[1]);
                eval(cmdline, pipe_cmd->left);
            }
            close(fds[0]);
            close(fds[1]);
            // 等待两个子进程完成运行
            wait(0);
            wait(0);
            exit(0);
            break;
        case REDIR:
            execredir((struct redircmd *)command);
            break;
        default:
            fprintf(stderr, "unknown command type\n");
    }
}

/**
 * getexeccmd - 将 command 转化为 struct execcmd *,
 * 主要是应对 exec，exec 后运行的命令的类型不确定
 */
struct execcmd *getexeccmd(struct cmd *command) {
    struct execcmd *exec_cmd;
    struct redircmd *redir_cmd;
    struct pipecmd *pipe_cmd;
    switch (command->type) {
        case EXEC:
            exec_cmd = (struct execcmd *)command;
            break;
        case REDIR:
            redir_cmd = (struct redircmd *)command;
            exec_cmd = getexeccmd(redir_cmd->command);
            break;
        case PIPE:
            pipe_cmd = (struct pipecmd *)command;
            exec_cmd = getexeccmd(pipe_cmd->left);
            break;
    }
    return exec_cmd;
}

/**
 * is_buiit_in_command - 判断是否为内部命令，若是内部命令，
 * 则运行内部命令，并返回非零值；否则，返回 0，表示当前命令不为
 * 内部命令
 */
int is_built_in_command(struct cmd *command) {
    struct execcmd *exec_cmd = getexeccmd(command);
    
    if (strcmp(exec_cmd->argv[0], "bg") == 0) {
        bg_imp(exec_cmd->argc, exec_cmd->argv);
        return 1;
    } else if (strcmp(exec_cmd->argv[0], "cd") == 0) {
        cd_imp(exec_cmd->argv[1]);
        return 2;
    } else if (strcmp(exec_cmd->argv[0], "clr") == 0) {
        clr_imp();
        return 3;
    } else if (strcmp(exec_cmd->argv[0], "dir") == 0) {
        dir_imp(exec_cmd->argv[1]);
        return 4;
    } else if (strcmp(exec_cmd->argv[0], "echo") == 0) {
        echo_imp(exec_cmd->argv);
        return 5;
    } else if (strcmp(exec_cmd->argv[0], "exec") == 0) {
        exec_imp(command);
        exit(0);
    } else if (strcmp(exec_cmd->argv[0], "exit") == 0) {
        exit(0);
    } else if (strcmp(exec_cmd->argv[0], "fg") == 0) {
        int ret = fg_imp(exec_cmd->argc, exec_cmd->argv);
        if (!ret) {
            waitfg();
        }
        return 8;
    } else if (strcmp(exec_cmd->argv[0], "help") == 0) {
        help_imp();
        return 9;
    } else if (strcmp(exec_cmd->argv[0], "jobs") == 0) {
        listjobs();
        return 10;
    } else if (strcmp(exec_cmd->argv[0], "pwd") == 0) {
        printf("%s\n", pwd);
        return 11;
    } else if (strcmp(exec_cmd->argv[0], "set") == 0) {
        set_imp();
        return 12;
    } else if (strcmp(exec_cmd->argv[0], "test") == 0) {
        test_imp(exec_cmd->argc, exec_cmd->argv);
        return 13;
    } else if (strcmp(exec_cmd->argv[0], "time") == 0) {
        time_imp();
        return 14;
    } else if (strcmp(exec_cmd->argv[0], "umask") == 0) {
        umask_imp(exec_cmd->argv);
        return 15;
    }
    return 0;
}

/**
 * exec_imp - exec 内部命令的实现
 */
void exec_imp(struct cmd *command) {
    struct execcmd *exec_cmd;
    struct pipecmd *pipe_cmd;
    int i, built_in;
    int fd[2];

    switch (command->type) {
        case EXEC:  // 直接执行
            exec_cmd = (struct execcmd *)command;
            if (strcmp(exec_cmd->argv[0], "exec") == 0) {   // 若为 exec 命令，则需要将命令行参数向前移动一位
                for (i = 0; i < exec_cmd->argc; i++) {
                    exec_cmd->argv[i] = exec_cmd->argv[i + 1];
                }
            }
            int built_in = is_built_in_command(command);

            if (!built_in) {
                execve(exec_cmd->argv[0], exec_cmd->argv, __environ);
                fprintf(stderr, "%s: 未找到命令\n", exec_cmd->argv[0]);
                exit(1);
            }
            exit(0);
            break;
        case PIPE:  // 管道
            pipe_cmd = (struct pipecmd *)command;
            if (pipe(fd) == -1) {
                fprintf(stderr, "pipe error: %s\n", strerror(errno));
            }

            if (fork() == 0) {  //  right
                close(0);
                dup(fd[0]);     // 将管道复制到标准输入上
                close(fd[0]);
                close(fd[1]);
                exec_imp(pipe_cmd->right);
            }

            if (fork() == 0) {    //  left
                close(1);
                dup(fd[1]);       // 将管道复制到标准输出上
                close(fd[0]);
                close(fd[1]);
                exec_imp(pipe_cmd->left);
            }
            close(fd[0]);
            close(fd[1]);
            // 等待两个子进程完成运行
            wait(0);
            wait(0);
            exit(0);
            break;
        case REDIR: // 重定向
            execredir((struct redircmd *)command);
            break;
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

/**
 * clearjob - 清空 job_t 结构体
 */
void clearjob(struct job_t *job) {
    *(job->cmdline) = '\0';
    if (job->command) {  // 释放 command 资源
        free_cmd(job->command);
    }
    job->command = NULL;
    job->jid = 0;
    job->pid = 0;
    job->state = INVALID;
}

/**
 * initjob - 初始化 job_t 数组
 */
void initjob() {
    for (int i = 0; i < MAXJOBS; i++) {
        clearjob(&jobs[i]);
    }
}

/**
 * addjob - 向 job_t 数组中添加一个 job，返回刚设置的结构体
 */
struct job_t *addjob(char *cmdline, int bgfg, struct cmd *command, pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state == INVALID) {
            jobs[i].state = bgfg ? BG : FG;
            jobs[i].pid = pid;
            jobs[i].command = command;
            strcpy(jobs[i].cmdline, cmdline);
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS) {
                nextjid = 1;
            }
            return &jobs[i];
        }
    }
    fprintf(stderr, "作业数量过多\n");
    return NULL;
}

/**
 * maxjid - 返回当前最大的 jid
 */
int maxjid() {
    int jid = 0;
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state != INVALID && jobs[i].jid > jid) {
            jid = jobs[i].jid;
        }
    }
    return jid;
}

/**
 * deljob - 删除 jobs 数组中 pid 等于传入的 pid 的结构体
 */
int deljob(pid_t pid) {
    if (pid < 0) {
        return 0;
    }

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid() + 1;  // 设置下一个 jid
            return 1;
        }
    }
    return 0;
}

/**
 * getjobjid - 通过 jid 获得结构体
 */
struct job_t *getjobjid(int jid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid == jid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/**
 * getjobpid - 通过 pid 获得结构体
 */
struct job_t *getjobpid(pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/**
 * fg_imp - 将某一后台作业或被暂停的作业传递至前台运行，作业号由
 * 参数传递
 */
int fg_imp(int argc, char *argv[]) {
    int jid;
    if (*argv[1] == '%') {
        jid = atoi(argv[1] + 1);
    } else {
        jid = atoi(argv[1]);
    }
    sigset_t oldmask, mask;
    sigfillset(&mask);
    // 保护 jobs 数组
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    struct job_t *job = getjobjid(jid);
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    if (job == NULL) {
        fprintf(stderr, "fg: %s: 无此任务\n", argv[1]);
        return 1;
    }
    fgpid = job->pid;
    sigprocmask(SIG_BLOCK, &mask, NULL);
    job->state = FG;        // 设置 job 的状态为 FG
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    kill(-fgpid, SIGCONT);  // 传递 SIGCONT 信号，恢复运行
    return 0;
}

/**
 * bg_imp - 将某一被暂停的作业转移到后台运行
 */
int bg_imp(int argc, char *argv[]) {
    int jid;
    if (*argv[1] == '%') {
        jid = atoi(argv[1] + 1);
    } else {
        jid = atoi(argv[1]);
    }
    sigset_t oldmask, mask;
    sigfillset(&mask);
    // 保护 jobs 数组
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    struct job_t *job = getjobjid(jid);
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    if (job == NULL) {
        fprintf(stderr, "fg: %s: 无此任务\n", argv[1]);
        return 1;
    }
    sigprocmask(SIG_BLOCK, &mask, NULL);
    job->state = BG;    // 设置 job 的状态为 BG
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    kill(-job->pid, SIGCONT);  // 传递 SIGCONT 信号，恢复运行
    return 0;
}


/**
 * waitfg - 等待前台进程完成
 */
void waitfg() {
    sigset_t mask;
    sigemptyset(&mask);
    // 当 fgpid 未被 sigchld_handler 清空时，
    // 阻塞进程，若收到信号，则调用信号处理函数，
    // 如果 fgpid 被清空，则退出循环，否则，持续循环
    while (fgpid != 0) {
        sigsuspend(&mask);
    }
}

/**
 * listjobs - 列出 jobs 数组中所有状态不为 INVALID 的结构体
 */
void listjobs() {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state != INVALID) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                    i, jobs[i].state);
            }
            printf("%s\n", jobs[i].cmdline);
        }
    }
}

/******************************
 * 信号处理函数
********************************/
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, oldaction;

    sigemptyset(&action.sa_mask);
    action.sa_handler = handler;
    action.sa_flags = SA_RESTART;

    if (sigaction(signum, &action, &oldaction) < 0) {
        fprintf(stderr, "Signal error\n");
        exit(1);
    }
    return (oldaction.sa_handler);
}

/**
 * sigchld_handler - 当子进程退出，或被停止，或被用户中断时，调用该函数，
 * 该函数调用 waitpid 获得当前退出或停止的子进程的 pid，然后判断是否为当前的前台进程，
 * 若是，则清空 fgpid；若不是，则判断当前进程退出的原因，若是被停止或中断，则输出
 * 相关的信息
 * 注意，我们要使用 while 循环，因为当这个函数被调用时，可能此时有多个子进程需要处理
 */
void sigchld_handler(int sig) {
    sigset_t oldmask, mask;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        sigfillset(&mask);
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        if (fgpid == pid) {  // 当前的前台进程
            fgpid = 0;
        }
        if (WIFEXITED(status)) {          // 正常退出
            deljob(pid);
        } else if (WIFSTOPPED(status)) {  // SIGTSTP
            struct job_t *job = getjobpid(pid);
            printf("[%d] (%d) 已停止 %s\n", job->jid, job->pid, job->cmdline);
            job->state = ST;
        } else if (WIFSIGNALED(status)) {  // SIGINT
            struct job_t *job = getjobpid(pid);
            printf("Job [%d] (%d) 被信号终止\n", job->jid, job->pid);
            deljob(pid);
        }
        sigprocmask(SIG_SETMASK, &oldmask, &mask);
    }
}

/**
 * sigint_handler - 当传递 sigint 信号时调用函数，用户在 shell 中输入
 * ctrl+c，sigint 信号将传递给 shell，shell 进程将信号发送给当前的前台进程组
 */
void sigint_handler(int sig) {
    sigset_t mask, oldmask;
    sigfillset(&mask);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (fgpid == 0) {
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        return;
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    kill(-fgpid, sig);  // 向前台进程组传递信号
}

/**
 * sigtstp_handler - 当传递 sigstop 信号时调用函数，用户在 shell 中输入
 * ctrl+z，sigstop 信号将传递给 shell，shell 进程将信号发送给当前的前台进程组，
 * 停止前台进程
 */
void sigtstp_handler(int sig) {
    sigset_t mask, oldmask;
    sigfillset(&mask);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    if (fgpid == 0) {
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        return;
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    kill(-fgpid, sig);  // 向前台进程组传递信号
}