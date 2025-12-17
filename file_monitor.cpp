#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <dirent.h>
#include <map>
#include <set>
#include <signal.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))

class FileMonitor {
private:
    int fd;
    std::map<int, std::string> watch_descriptors;
    std::set<std::string> accessed_files;
    std::set<std::string> new_files;
    std::string log_file;
    int flush_interval;
    time_t last_flush;
    bool running;

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
            std::cout << "已加载 " << accessed_files.size() << " 个已记录的文件" << std::endl;
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

        std::cout << "已写入 " << new_files.size() << " 个新文件到 " << log_file << std::endl;
        new_files.clear();
        last_flush = time(nullptr);
    }

    int add_watch_recursive(const std::string& path) {
        struct stat path_stat;
        if (stat(path.c_str(), &path_stat) != 0) {
            std::cerr << "无法访问路径: " << path << std::endl;
            return -1;
        }

        int wd = inotify_add_watch(fd, path.c_str(), 
            IN_ACCESS | IN_MODIFY | IN_OPEN | IN_CLOSE | 
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
        
        if (wd == -1) {
            std::cerr << "无法监控路径: " << path << std::endl;
            return -1;
        }

        watch_descriptors[wd] = path;
        std::cout << "开始监控: " << path << std::endl;

        if (S_ISDIR(path_stat.st_mode)) {
            DIR* dir = opendir(path.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }
                    std::string subpath = path + "/" + entry->d_name;
                    if (entry->d_type == DT_DIR) {
                        add_watch_recursive(subpath);
                    }
                }
                closedir(dir);
            }
        }

        return wd;
    }



public:
    FileMonitor(const std::string& log_path, int interval) 
        : log_file(log_path), flush_interval(interval), running(true) {
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
        
        std::cout << "监控已启动，日志文件: " << log_file << std::endl;
        std::cout << "刷新间隔: " << flush_interval << " 秒" << std::endl;
        std::cout << "按 Ctrl+C 停止监控" << std::endl;

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
                                std::cout << "检测到文件访问: " << full_path << std::endl;
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

void signal_handler(int signum) {
    std::cout << "\n接收到停止信号，正在保存日志..." << std::endl;
    if (g_monitor) {
        g_monitor->stop();
    }
}

void print_usage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项] <目录1> [目录2] ..." << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -l <日志文件>    指定日志文件路径 (默认: file_monitor.log)" << std::endl;
    std::cout << "  -i <秒数>        指定刷新间隔 (默认: 60秒)" << std::endl;
    std::cout << "  -h               显示帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program_name << " /home/user/documents" << std::endl;
    std::cout << "  " << program_name << " -l monitor.log -i 30 /tmp /var/log" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string log_file = "file_monitor.log";
    int flush_interval = 60;
    std::vector<std::string> watch_paths;

    int opt;
    while ((opt = getopt(argc, argv, "l:i:h")) != -1) {
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

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        FileMonitor monitor(log_file, flush_interval);
        g_monitor = &monitor;

        for (const auto& path : watch_paths) {
            monitor.add_watch_path(path);
        }

        monitor.start_monitoring();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
