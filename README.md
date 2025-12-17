# 文件系统监控工具

一个用C++编写的Linux文件系统监控工具，使用inotify API实时监控指定目录的文件访问活动。

## 功能特性

- 监控多个目录及其子目录
- 记录被访问的文件路径，每个文件仅记录一次
- 启动时自动加载已有日志，避免重复记录
- 定期自动将新文件写入磁盘
- 支持递归监控子目录
- 优雅处理退出信号（Ctrl+C）
- 无需外部依赖库

## 编译

```bash
make
```

或手动编译：

```bash
g++ -std=c++11 -Wall -O2 -o file_monitor file_monitor.cpp
```

## 使用方法

### 基本用法

```bash
./file_monitor <目录1> [目录2] ...
```

### 选项

- `-l <日志文件>` - 指定日志文件路径（默认：file_monitor.log）
- `-i <秒数>` - 指定日志刷新到磁盘的间隔（默认：60秒）
- `-h` - 显示帮助信息

### 示例

监控单个目录：
```bash
./file_monitor /home/user/documents
```

监控多个目录，自定义日志文件和刷新间隔：
```bash
./file_monitor -l monitor.log -i 30 /tmp /var/log
```

监控当前目录：
```bash
./file_monitor .
```

## 日志格式

日志文件中每行记录一个被访问的文件路径，每个文件只出现一次：

```
/home/user/documents/file.txt
/home/user/documents/report.doc
/home/user/documents/new_file.txt
/tmp/test.log
```

程序启动时会自动加载已有日志文件，确保不会重复记录已经出现过的文件。

## 停止监控

按 `Ctrl+C` 停止监控，程序会自动保存所有未写入的日志。

## 系统要求

- Linux操作系统（支持inotify）
- GCC/G++ 编译器（支持C++11）

## 安装到系统

```bash
make install
```

这将把程序安装到 `/usr/local/bin/`，之后可以在任何位置直接运行：

```bash
file_monitor /path/to/monitor
```

## 卸载

```bash
make uninstall
```

## 注意事项

1. 监控大量文件可能会消耗较多系统资源
2. 需要有读取目标目录的权限
3. 某些系统操作可能产生大量事件，建议适当调整刷新间隔
4. 日志文件会持续增长，建议定期清理或使用日志轮转

## 技术实现

- 使用Linux inotify API进行文件系统事件监控
- 递归监控子目录
- 内存缓冲日志条目，定期批量写入磁盘以提高性能
- 信号处理确保程序退出时数据不丢失
