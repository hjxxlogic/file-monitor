#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#include <map>
#include <set>
#include <signal.h>
#include <errno.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
#define LOCK_FILE "/var/run/lock/file_monitor.lock"

class FileMonitor {
private:
    int fd;
    std::map<int, std::string> watch_descriptors;
    std::set<std::string> watched_targets;
    std::set<std::string> accessed_files;
    std::set<std::string> new_files;
    std::string log_file;
    int flush_interval;
    time_t last_flush;
    bool running;
    bool silent;

    void load_existing_files() {
        std::ifstream ifs(log_file);
        if (ifs.is_open()) {
            std::string line;
            while (std::getline(ifs, line)) {
                if (!line.empty()) {
                    accessed_files.insert(line);
                }
            }
            ifs.close();
            if (!silent) {
                std::cout << "已加载 " << accessed_files.size() << " 个已记录的文件" << std::endl;
            }
        }
    }

    void add_file_access(const std::string& filepath) {
        if (accessed_files.find(filepath) == accessed_files.end()) {
            accessed_files.insert(filepath);
            new_files.insert(filepath);
        }
    }

    void flush_to_disk() {
        if (new_files.empty()) {
            return;
        }

        std::ofstream ofs(log_file, std::ios::app);
        if (!ofs.is_open()) {
            std::cerr << "无法打开日志文件: " << log_file << std::endl;
            return;
        }

        for (const auto& filepath : new_files) {
            ofs << filepath << std::endl;
        }
        ofs.close();

        if (!silent) {
            std::cout << "已写入 " << new_files.size() << " 个新文件到 " << log_file << std::endl;
        }
        new_files.clear();
        last_flush = time(nullptr);
    }

    std::string get_absolute_path(const std::string& path) {
        char resolved_path[PATH_MAX];
        if (realpath(path.c_str(), resolved_path) != nullptr) {
            return std::string(resolved_path);
        }
        return path;
    }

    std::string get_absolute_path_nofollow(const std::string& path) {
        if (!path.empty() && path[0] == '/') {
            return path;
        }

        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            return path;
        }
        std::string abs = std::string(cwd);
        if (!abs.empty() && abs.back() != '/') {
            abs += '/';
        }
        abs += path;
        return abs;
    }

    int add_watch_recursive(const std::string& path) {
        struct stat path_stat;
        if (stat(path.c_str(), &path_stat) != 0) {
            std::cerr << "无法访问路径: " << path << std::endl;
            return -1;
        }

        std::string watch_path = get_absolute_path_nofollow(path);
        std::string target_path = get_absolute_path(path);
        if (watched_targets.find(target_path) != watched_targets.end()) {
            return 0;
        }

        int wd = inotify_add_watch(fd, watch_path.c_str(), 
            IN_ACCESS | IN_MODIFY | IN_OPEN | IN_CLOSE | 
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DONT_FOLLOW);
        
        if (wd == -1) {
            std::cerr << "无法监控路径: " << watch_path << std::endl;
            return -1;
        }

        watch_descriptors[wd] = watch_path;
        watched_targets.insert(target_path);
        if (!silent) {
            std::cout << "开始监控: " << watch_path << std::endl;
        }

        if (S_ISDIR(path_stat.st_mode)) {
            DIR* dir = opendir(watch_path.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }
                    std::string subpath = watch_path + "/" + entry->d_name;

                    struct stat sub_stat;
                    if (stat(subpath.c_str(), &sub_stat) == 0 && S_ISDIR(sub_stat.st_mode)) {
                        add_watch_recursive(subpath);
                    }
                }
                closedir(dir);
            }
        }

        return wd;
    }



public:
    FileMonitor(const std::string& log_path, int interval, bool silent_mode = false) 
        : log_file(log_path), flush_interval(interval), running(true), silent(silent_mode) {
        fd = inotify_init();
        if (fd < 0) {
            throw std::runtime_error("无法初始化 inotify");
        }
        last_flush = time(nullptr);
        load_existing_files();
    }

    ~FileMonitor() {
        flush_to_disk();
        for (auto& pair : watch_descriptors) {
            inotify_rm_watch(fd, pair.first);
        }
        close(fd);
    }

    void add_watch_path(const std::string& path) {
        add_watch_recursive(path);
    }

    void start_monitoring() {
        char buffer[BUF_LEN];
        
        if (!silent) {
            std::cout << "监控已启动，日志文件: " << log_file << std::endl;
            std::cout << "刷新间隔: " << flush_interval << " 秒" << std::endl;
            std::cout << "按 Ctrl+C 停止监控" << std::endl;
        }

        while (running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
            
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }

            if (ret > 0 && FD_ISSET(fd, &fds)) {
                int length = read(fd, buffer, BUF_LEN);
                if (length < 0) {
                    break;
                }

                int i = 0;
                while (i < length) {
                    struct inotify_event* event = (struct inotify_event*)&buffer[i];
                    
                    if (event->len > 0) {
                        auto it = watch_descriptors.find(event->wd);
                        if (it != watch_descriptors.end()) {
                            std::string full_path = it->second + "/" + event->name;
                            
                            if (!(event->mask & IN_ISDIR)) {
                                add_file_access(full_path);
                                if (!silent) {
                                    std::cout << "检测到文件访问: " << full_path << std::endl;
                                }
                            }

                            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                                add_watch_recursive(full_path);
                            }
                        }
                    }

                    i += EVENT_SIZE + event->len;
                }
            }

            time_t now = time(nullptr);
            if (now - last_flush >= flush_interval) {
                flush_to_disk();
            }
        }
    }

    void stop() {
        running = false;
    }
};

FileMonitor* g_monitor = nullptr;
int g_lock_fd = -1;

void signal_handler(int signum) {
    if (g_monitor) {
        g_monitor->stop();
    }
}

void cleanup_lock() {
    if (g_lock_fd != -1) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        unlink(LOCK_FILE);
        g_lock_fd = -1;
    }
}

bool acquire_lock() {
    // 尝试创建锁目录（如果不存在）
    mkdir("/var/run/lock", 0755);
    
    g_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd == -1) {
        std::cerr << "错误: 无法创建锁文件 " << LOCK_FILE << std::endl;
        std::cerr << "提示: 可能需要 root 权限，或使用其他目录" << std::endl;
        return false;
    }
    
    // 尝试获取排他锁（非阻塞）
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            std::cerr << "错误: 程序已经在运行中" << std::endl;
            std::cerr << "锁文件: " << LOCK_FILE << std::endl;
            
            // 尝试读取锁文件中的PID
            char pid_buf[32];
            ssize_t n = read(g_lock_fd, pid_buf, sizeof(pid_buf) - 1);
            if (n > 0) {
                pid_buf[n] = '\0';
                std::cerr << "运行中的进程 PID: " << pid_buf << std::endl;
            }
        } else {
            std::cerr << "错误: 无法获取文件锁: " << strerror(errno) << std::endl;
        }
        close(g_lock_fd);
        g_lock_fd = -1;
        return false;
    }
    
    // 写入当前进程的PID
    if (ftruncate(g_lock_fd, 0) == -1) {
        // 忽略错误，继续运行
    }
    std::string pid_str = std::to_string(getpid()) + "\n";
    if (write(g_lock_fd, pid_str.c_str(), pid_str.length()) == -1) {
        // 忽略错误，继续运行
    }
    
    // 注册清理函数
    atexit(cleanup_lock);
    
    return true;
}

void print_usage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项] <目录1> [目录2] ..." << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -l <日志文件>    指定日志文件路径 (默认: file_monitor.log)" << std::endl;
    std::cout << "  -i <秒数>        指定刷新间隔 (默认: 60秒)" << std::endl;
    std::cout << "  -s               静默模式，不输出到标准输出" << std::endl;
    std::cout << "  -h               显示帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program_name << " /home/user/documents" << std::endl;
    std::cout << "  " << program_name << " -l monitor.log -i 30 /tmp /var/log" << std::endl;
    std::cout << "  " << program_name << " -s -l monitor.log /home/user/documents" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string log_file = "file_monitor.log";
    int flush_interval = 60;
    bool silent_mode = false;
    std::vector<std::string> watch_paths;

    int opt;
    while ((opt = getopt(argc, argv, "l:i:sh")) != -1) {
        switch (opt) {
            case 'l':
                log_file = optarg;
                break;
            case 'i':
                flush_interval = std::atoi(optarg);
                if (flush_interval <= 0) {
                    std::cerr << "错误: 刷新间隔必须大于0" << std::endl;
                    return 1;
                }
                break;
            case 's':
                silent_mode = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    for (int i = optind; i < argc; i++) {
        watch_paths.push_back(argv[i]);
    }

    if (watch_paths.empty()) {
        std::cerr << "错误: 请至少指定一个要监控的目录" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // 获取文件锁，确保只运行一个实例
    if (!acquire_lock()) {
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        FileMonitor monitor(log_file, flush_interval, silent_mode);
        g_monitor = &monitor;

        for (const auto& path : watch_paths) {
            monitor.add_watch_path(path);
        }

        monitor.start_monitoring();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        cleanup_lock();
        return 1;
    }

    cleanup_lock();
    return 0;
}
