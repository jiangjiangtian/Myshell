#! /bin/bash
# 姓名：蒋添 学号：3210102488
# 文件同步和备份

if [[ $# -ne 2 ]]
then
	echo "Usage: dirsync dir1 dir2"
	exit 1
fi

if ! [[ -d $1 ]]
then
	echo "$1 不为目录"
	exit 1
fi

if ! [[ -d $2 ]]
then
	echo "$2 不为目录"
	exit 1
fi

if [[ $1 == $2 ]]
then
	echo "$1 与 $2 需要不同"
	exit 1
fi

# find $1 | tail -n +2 | xargs -I {} cp -u -R -f -p {} $2 2>/dev/null
# 将 $1 下的所有文件和文件夹复制到 $2 下
cp -u -R -f -p $1/. $2

# 同步功能，删除在 $2 中存在但不在 $1 中存在的文件和文件夹
rm_not_exist() {
# for file in $(find $2 -maxdepth 1 | tail -n +2)
for file in $2/*
do
	if [[ -d $file ]]
	then
		ls $1/${file##*/} > /dev/null 2>&1
		if [[ $? -ne 0 ]]
		then
			# 文件夹在 $1 中不存在
			rm -rf $file
			if [[ $? -eq 0 ]]
			then
				echo "删除文件夹：$file"
			fi
		else
			$0 $1/${file##*/} $file 
		fi
	else
		ls $1 | grep ${file##*/} > /dev/null 2>&1
		# 若在 $1 中找不到这个文件名，则将其删除
		if [[ $? -ne 0 ]]
		then
			rm -rf $file
			if [[ $? -eq 0 ]]
			then
				echo "删除文件 $file"
			fi
		fi
	fi
done
return 0
}

rm_not_exist $1 $2
