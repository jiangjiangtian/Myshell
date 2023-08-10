#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

#include "built_in_command.h"
#define MAXLEN 512
extern char pwd[MAXLEN];
extern mode_t mode;

/**
 * cd_imp - cd 命令的实现，更改环境变量 PWD
 */
void cd_imp(const char *dir_path) {
    if (dir_path == NULL || strcmp(dir_path, ".") == 0) {
        // 如果 cd 当前目录或给定目录为空，则输出当前目录
        printf("%s\n", pwd);
        return;
    }
    // 首先判断是否为目录
    struct stat buf;
    stat(dir_path, &buf);
    if (!S_ISDIR(buf.st_mode)) {
        fprintf(stderr, "cd: %s: 没有那个文件或目录\n", dir_path);
        return;
    }

    // 更改当前工作目录，然后修改 pwd 变量
    if (chdir(dir_path) != 0) {
        fprintf(stderr, "cd 发生错误\n");
        return;
    }
    getcwd(pwd, MAXLEN);
    // 更改环境变量
    setenv("PWD", pwd, 1);
}

/**
 * dir_imp - 列出目录 dir_path 中的内容，若 dir_path 为空，
 * 则列出当前工作目录下的内容
 */
void dir_imp(char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    if (dir_path) {
        dir = opendir(dir_path);
        if (dir == NULL) {
            fprintf(stderr, "%s 不为目录\n", dir_path);
        }
    } else {
        dir = opendir(pwd);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        printf("%s ", entry->d_name);
    }
    printf("\n");
    closedir(dir);
}

/**
 * echo_imp - 在屏幕上输出 argv 中传入的字符串
 */
void echo_imp(char *argv[]) {
    char *str = argv[1];
    for (int i = 1; str != NULL; i++, str = argv[i]) {
        printf("%s ", str);
    }
    printf("\n");
}

/**
 * clr_imp - 刷新屏幕，持续输出换行符清空当前屏幕，
 * 然后将光标移动到最顶端
 */
void clr_imp() {
    // 获取终端大小
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == -1) {
        fprintf(stderr, "clr: 出现错误\n");
        return;
    }
    for (int i = 0; i < size.ws_row; i++) {
        printf("\n");
    }
    printf("\033[%dA", size.ws_row);
}

/**
 * time_imp - 显示当前时间
 */
void time_imp() {
    time_t current_time;
    struct tm *cur_time;
    time(&current_time);
    cur_time = localtime(&current_time);
    printf("%s", asctime(cur_time));
}

/**
 * help_imp - 输出帮助手册
 */
void help_imp() {
    printf("下面这些 shell 命令是内部定义的\n\n");
    printf("help 帮助手册\n");
    printf("bg [任务声明 ...]\n");
    printf("fg [任务声明]\n");
    printf("exit 退出 shell\n");
    printf("pwd 显示当前目录\n");
    printf("cd <目录> 更改当前目录\n");
    printf("jobs 列出当前所有的任务\n");
    printf("umask 模式]\n");
    printf("test [表达式]\n");
    printf("time 显示当前时间\n");
    printf("echo <comment>\n");
    printf("dir [目录] 列出目录的内容\n");
    printf("set 显示所有的环境变量\n");
    printf("clr 清屏\n");
}

/**
 * set_imp - 输出所有的环境变量
 */
void set_imp(void) {
    char *str = __environ[0];
    for (int i = 0; str != NULL; i++, str = __environ[i]) {
        printf("%s\n", str);
    }
}

/**
 * umask_imp - 设置创建文件时的权限，如果没有参数，则输出当前的设置
 */
void umask_imp(char *argv[]) {
    if (argv[1] == NULL) {  // 没有参数，输出当前的设置
        printf("%u\n", mode);
    }
    // 判断传入参数是否合法
    int len = strlen(argv[1]);
    if (len > 4) {
        fprintf(stderr, "参数太长：最多三位\n");
        return;
    }
    for (int i = len - 1; i >= 0; i--) {
        if (argv[1][i] < '0' && argv[1][i] > '7') {
            fprintf(stderr, "参数不合法，每一位只能为 0 到 7\n");
            return;
        }
    }
    // 设置
    mode = atoi(argv[1]);
    umask(mode);
}