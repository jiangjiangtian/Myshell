#! /bin/bash
# 判断输入的字符串是否是回文串，忽略非字母
# 姓名：蒋添 学号：3210102488

# 判断传入的字符是否为字母
is_alpha() {
	if [[ $1 > 'a' ]] && [[ $1 < 'z' ]]
	then
		return 0
	elif [[ $1 > 'A' ]] && [[ $1 < 'Z' ]]
	then
		return 0
	elif [[ $1 == 'a' ]] || [[ $1 == 'z' ]] || [[ $1 == 'A' ]] || [[ $1 == 'Z' ]]
	then
		return 0
	else
		return 1
	fi
}

echo -n "请输入字符串："
read str
len=${#str}	# 计算字符串的长度
for (( i = 1, j = len; i < j; i++, j-- ))
do
	# 找到下一个为字母的位置
	ch1=$(echo $str | cut -b $i)
	while ! is_alpha $ch1
	do
		i=$(($i + 1))
		ch1=$(echo $str | cut -b $i)
	done
	# 找到下一个为字母的位置
	ch2=$(echo $str | cut -b $j)
	while ! is_alpha $ch2
	do
		j=$(($j - 1))
		ch2=$(echo $str | cut -b $j)
	done
	# 如果 i 大于 j，则提前退出
	if [[ $i -gt $j ]]
	then
		echo "$str 不是回文串"
		exit 1
	fi
	
	if [[ $ch1 != $ch2 ]]
	then
		echo "$str 不是回文串"
		exit 1
	fi
done
echo "$str 是回文串"
exit 0
