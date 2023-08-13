#! /bin/bash
# 程序名：count.sh
# 作者：蒋添 学号：3210102488
echo -n "$1下普通文件数目："
ls -l $1 | grep ^- | wc -l
echo -n "$1下子目录数目："
ls -l $1 | grep ^d | wc -l
echo -n "$1下可执行文件数目："
ls -F $1 | grep "*" | wc -l
echo -n "$1下所有普通文件字节数总和："
find $1 -maxdepth 1 -type f -exec du -cb {} + | tail -n1 | cut -f1

