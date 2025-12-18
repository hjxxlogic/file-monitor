#!/bin/bash
# 使用示例

echo "文件系统监控工具使用示例"
echo ""

# 示例1：监控单个目录
echo "示例1：监控当前目录，日志保存到 access.log，每30秒刷新一次"
echo "命令: ./file_monitor -l access.log -i 30 ."
echo ""

# 示例2：监控多个目录
echo "示例2：监控多个目录"
echo "命令: ./file_monitor -l monitor.log -i 60 /home/user/documents /tmp"
echo ""

# 示例3：使用默认设置
echo "示例3：使用默认设置（日志文件：file_monitor.log，刷新间隔：60秒）"
echo "命令: ./file_monitor /path/to/watch"
echo ""

echo "特性说明："
echo "- 每个被访问的文件只会在日志中记录一次（一行一个文件路径）"
echo "- 程序启动时自动加载已有日志，避免重复记录"
echo "- 支持多次运行，保持去重效果"
echo "- 按 Ctrl+C 停止监控，自动保存所有未写入的日志"
