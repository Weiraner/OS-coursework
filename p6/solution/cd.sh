#!/bin/bash

# 检查参数是否提供
if [ $# -lt 2 ]; then
    echo "Usage: $0 <number_of_files> <file_prefix>"
    echo "Example: $0 5 disk"
    exit 1
fi

# 参数解析
num_files=$1
file_prefix=$2

# 循环生成文件
for i in $(seq 1 $num_files); do
    filename="${file_prefix}${i}"
    echo "Creating file: $filename"
    dd if=/dev/zero of=$filename bs=1M count=1 status=none
    echo "File $filename created successfully."
done

echo "All $num_files files created."

