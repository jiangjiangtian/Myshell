#! /bin/bash
# 姓名：蒋添 学号：3210102488
# 功能类似于 vim 的简易编辑器

function output() {
	# 首先清空屏幕，然后输出当前的所有行
	clear
	for (( i = 0; i <= $len; i++ ))
	do
		if [[ $i -ne $len ]]
		then
			echo ${text[$i]}
		else
			# 若为最后一行，则不输出换行符
			echo -n ${text[$i]}
		fi
	done
	# 光标回到之前的位置，此时光标在最末尾
	offset=$(($len-$row))
	# 如果偏移量大于 0，则将光标向上移动
	if [[ $offset -gt 0 ]]
	then
		echo -n -e "\033[${offset}A"
	fi
	# 调整光标所在列
	offset=$((${#text[$len]}))
	echo -n -e "\033[${offset}D"
	# 如果当前列号大于 0，则将光标向右移动
	if [[ $col -gt 0 ]]
	then
		echo -n -e "\033[${col}C"
	fi
}

function newline() {
	# 当输入回车时，将调用这个函数
	# 更新 text 数组，输出 text 数组
	# 最后将光标移动到正确位置
	for (( i = $len; i > $row; i-- ))
	do
		text[$(($i+1))]=${text[$i]}
	done
	str_len=${#text[$row]}
	str_len=$(($str_len-$col))
	text[$(($row+1))]=${text[$row]:$col:$str_len}
	text[$row]=${text[$row]:0:$col}
	row=$(($row+1))
	col=0
	len=$(($len+1))
	output
}

function replacement() {
	# 输入 c 进入替换模式
	# 再根据下一个输入选择替换的内容
	# 退出后将进入插入模式
	str_len=${#text[$row]}
	read -s -n 1 option
	case $option in
		l)
			# 替换下一个字符
			if [[ $len -lt $(($str_len-1)) ]]
			then
				substr=${text[$row]:$(($col+1)):$(($str_len-$col-1))}
			else
				substr=""
			fi
			text[$row]=${text[$row]:0:$col}$substr
			;;
		h)
			# 替换前一个字符
			if [[ $col -gt 0 ]]
			then
				substr=${text[$row]:0:$(($col-1))}
				new_col=$(($col-1))
			else
				substr=""
				new_col=0
			fi
			text[$row]=$substr${text[$row]:$(($col)):$(($str_len-$col))}
			col=$new_col
			;;
		j)
			# 替换当前行和下一行
			for (( i = $row; i < $len; i++ ))
			do
				text[$i]=${text[$(($i+1))]}
			done
			text[$row]=""
			col=0
			if [[ $len -gt 0 ]]
			then
				len=$(($len-1))
			fi
			;;
		k)
			# 替换当前行和上一行
			row=$(($row-1))
			for (( i = $row; i < $len; i++ ))
			do
				text[$i]=${text[$(($i+1))]}
			done
			text[$row]=""
			col=0
			if [[ $len -gt 0 ]]
			then
				len=$(($len-1))
			fi
			;;
	esac
	# 刷新屏幕
	output
}

function deletion() {
	# 输入 d 进入删除模式
	# 再根据下一个输入选择删除的内容
	str_len=${#text[$row]}
	read -s -n 1 option
	case $option in
		l)
			# 删除下一个字符
			if [[ $len -lt $(($str_len-1)) ]]
			then
				substr=${text[$row]:$(($col+1)):$(($str_len-$col-1))}
			else
				substr=""
			fi
			text[$row]=${text[$row]:0:$col}$substr
			;;
		h)
			# 删除前一个字符
			if [[ $col -gt 0 ]]
			then
				substr=${text[$row]:0:$(($col-1))}
				new_col=$(($col-1))
			else
				substr=""
				new_col=0
			fi
			text[$row]=$substr${text[$row]:$(($col)):$(($str_len-$col))}
			col=$new_col
			;;
		j)
			# 删除当前行和下一行
			for (( i = $row; i < $(($len-1)); i++ ))
			do
				text[$i]=${text[$(($i+2))]}
			done
			col=0
			if [[ $len -gt 1 ]]
			then
				len=$(($len-2))
			fi
			;;
		k)
			# 删除当前行和上一行
			row=$(($row-1))
			for (( i = $row; i < $(($len-1)); i++ ))
			do
				text[$i]=${text[$(($i+2))]}
			done
			text[$row]=""
			col=0
			if [[ $len -gt 1 ]]
			then
				len=$(($len-2))
			fi
			;;
	esac
	# 刷新屏幕
	output
}

function insertion() {
	# 一个一个读入字符，若为 Esc，则退出插入模式
	while true
	do
		read -s -n 1 key 
		# 若为换行，则进行换行
		if [[ $key == `echo` ]]
		then
			newline
			continue
		fi
		# 若为 Esc，则退出函数
		if [[ $key == $'\E' ]]
		then
			return 0
		fi
		# 插入字符，重新输出文本，并更新光标位置
		str_len=${#text[$row]}
		if [[ $col -eq ${#text[$row]} ]]
		then
			str_len=0
		else
			str_len=$(($str_len-$col))
		fi
		substr=${text[$row]:$(($col)):$str_len}
		text[$row]=${text[$row]:0:$((col))}$key$substr
		col=$(($col+1))
		# 刷新屏幕
		output
	done
}

# 刷新屏幕
clear
row=0				# 当前光标所在行号
col=0				# 当前光标所在列号
declare -a text 	# 文本数组
len=0				# 文本数组的大小

# 不将空格作为分割符，将其读入
ifs=$IFS
IFS=

# 若提供了文件，则先将文件内容输出在屏幕上
if [[ $# -gt 0 ]]
then
	# 获取文件的行号
	len=$(cat $1 | wc -l)
	# 一行一行的读入文本
	for (( i = 0; i <= $len; i++ ))
	do
		text[$i]=`cat $1 | head -n $(($i+1)) | tail -n 1`
	done
	# 刷新屏幕
	output
fi

# 主循环，根据输入进入相应的模式
while true
do
	read -s -n 1 option
	if [[ $? -ne 0 ]]
	then
		exit 0
	fi
	case $option in
	i)
		# i 模式：插入模式
		insertion
		;;
	c)
		# c 模式：替换模式
		replacement
		insertion
		;;
	a)
		# a 模式：增加模式
		if [[ $col -lt ${#text[$row]} ]]
		then
			echo -e -n "\033[1C"
			col=$(($col+1))
		fi
		insertion
		;;
	d)
		# d 模式：删除模式
		deletion
		;;	
	w)
		# 保存文件，文件名由参数传递
		if [[ $# -eq 0 ]]
		then
			echo "请提供文件名，使用：./editor filename"
			exit 1
		fi
		rm $1 2>/dev/null
		for (( i = 0; i <= $len; i++ ))
		do
			echo ${text[$i]} >> $1
		done
		;;
	q)
		# q: 退出编辑器
		break
		;;
	k)
		# 上移
		if [[ $row -gt 0 ]]
		then
			row=$(($row-1))
			col=0
		fi
		output
		;;
	j)
		# 下移
		if [[ $row -lt $len ]]
		then
			row=$(($row+1))
			col=0
		fi
		output
		;;
	h)
		# 左移
		if [[ $col -ge 0 ]]
		then
			col=$(($col-1))
		fi
		output
		;;
	l)
		# 右移
		str_len=${#text[$row]}
		if [[ $col -lt str_len ]]
		then
			col=$(($col+1))
		fi
		output
		;;
	esac	
done
# 恢复之前的分割符
IFS=$ifs
clear
