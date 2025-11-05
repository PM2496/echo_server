#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <ctime>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8888

class EchoServer
{
private:
    int listen_fd;
    int epoll_fd;
    int port;
    struct epoll_event *events;

    // 性能统计数据
    unsigned long long total_messages;
    unsigned long long total_bytes;
    time_t start_time;
    time_t first_message_time; // 记录首条消息的时间
    bool has_traffic;          // 标记是否有流量

    // 设置套接字为非阻塞模式
    int setNonBlocking(int fd)
    {
        // 获取文件状态标志
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            perror("fcntl F_GETFL");
            return -1;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            perror("fcntl F_SETFL");
            return -1;
        }
        return 0;
    }

    // 创建并绑定监听套接字
    int createListenSocket()
    {
        /**
         * AF_INET: IPv4协议
         * SOCK_STREAM: 提供有序、可靠、双向、基于连接的字节流。
         * 0: 给定套接字类型的默认协议
         **/
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == -1)
        {
            perror("socket");
            return -1;
        }

        // 设置地址复用
        int opt = 1;
        /**
         * SOL_SOCKET: 套接字级别选项
         * SO_REUSEADDR: 允许重用本地地址和端口
         **/
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        {
            perror("setsockopt");
            close(listen_fd);
            return -1;
        }

        // 绑定套接字
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        // 设置地址族、IP地址和端口号
        server_addr.sin_family = AF_INET;
        // 监听所有可用接口
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        {
            perror("bind");
            close(listen_fd);
            return -1;
        }

        // 开始监听
        /**
         * SOMAXCONN: 系统允许的最大连接队列长度
         **/
        if (listen(listen_fd, SOMAXCONN) == -1)
        {
            perror("listen");
            close(listen_fd);
            return -1;
        }

        // 设置非阻塞模式
        if (setNonBlocking(listen_fd) == -1)
        {
            close(listen_fd);
            return -1;
        }

        return 0;
    }

    // 处理新连接
    void handleAccept()
    {
        while (true)
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 所有连接都已处理完毕
                    break;
                }
                else
                {
                    perror("accept");
                    break;
                }
            }

            std::cout << "New connection from "
                      << inet_ntoa(client_addr.sin_addr) << ":"
                      << ntohs(client_addr.sin_port)
                      << " (fd=" << client_fd << ")" << std::endl;

            // 设置非阻塞模式
            if (setNonBlocking(client_fd) == -1)
            {
                close(client_fd);
                continue;
            }

            // 将新连接添加到epoll实例中
            struct epoll_event ev;
            /**
             * EPOLLIN: 监视读事件
             * EPOLLET: 边缘触发模式（即只在状态改变时通知）
             **/
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = client_fd;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
            {
                perror("epoll_ctl: client_fd");
                close(client_fd);
            }
        }
    }

    // 处理客户端数据
    void handleClient(int client_fd)
    {
        char buffer[BUFFER_SIZE];

        while (true)
        {
            ssize_t n = read(client_fd, buffer, sizeof(buffer));

            if (n > 0)
            {
                // 回显数据
                ssize_t written = 0;
                while (written < n)
                {
                    ssize_t w = write(client_fd, buffer + written, n - written);
                    if (w == -1)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // 无法立即写入，稍后重试
                            continue;
                        }
                        else
                        {
                            perror("write");
                            closeClient(client_fd);
                            return;
                        }
                    }
                    written += w;
                }

                // 更新统计信息
                total_messages++;
                total_bytes += n;

                // 记录首次消息时间
                if (!has_traffic)
                {
                    first_message_time = time(nullptr);
                    has_traffic = true;
                    std::cout << "First message received, performance tracking started." << std::endl;
                }
            }
            else if (n == 0)
            {
                // 客户端关闭连接
                std::cout << "Client disconnected (fd=" << client_fd << ")" << std::endl;
                closeClient(client_fd);
                break;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 所有数据都已读取完毕
                    break;
                }
                else
                {
                    perror("read");
                    closeClient(client_fd);
                    break;
                }
            }
        }
    }

    // 关闭客户端连接
    void closeClient(int client_fd)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
    }

    // 打印性能统计信息
    void printStats()
    {
        time_t now = time(nullptr);

        // 如果还没有流量，不输出统计
        if (!has_traffic)
        {
            std::cout << "\n=== Server Statistics ===" << std::endl;
            std::cout << "Waiting for traffic..." << std::endl;
            std::cout << "========================\n"
                      << std::endl;
            return;
        }

        // 使用首次消息时间计算实际运行时间
        double elapsed = difftime(now, first_message_time);
        double total_elapsed = difftime(now, start_time);

        if (elapsed > 0)
        {
            std::cout << "\n=== Server Statistics ===" << std::endl;
            std::cout << "Server uptime: " << total_elapsed << " seconds" << std::endl;
            std::cout << "Active time: " << elapsed << " seconds" << std::endl;
            std::cout << "Total messages: " << total_messages << std::endl;
            std::cout << "Total bytes: " << total_bytes << std::endl;
            std::cout << "Messages/sec: " << (total_messages / elapsed) << std::endl;
            std::cout << "Throughput: " << (total_bytes / elapsed / 1024.0) << " KB/s" << std::endl;
            std::cout << "========================\n"
                      << std::endl;
        }
    }

public:
    EchoServer(int p = DEFAULT_PORT)
        : listen_fd(-1), epoll_fd(-1), port(p), events(nullptr),
          total_messages(0), total_bytes(0), has_traffic(false)
    {
        start_time = time(nullptr);
        first_message_time = 0;
    }

    ~EchoServer()
    {
        if (listen_fd != -1)
            close(listen_fd);
        if (epoll_fd != -1)
            close(epoll_fd);
        if (events != nullptr)
            delete[] events;
    }

    int start()
    {
        // 创建监听套接字
        if (createListenSocket() == -1)
        {
            return -1;
        }

        std::cout << "Echo server listening on port " << port << std::endl;

        // 创建epoll实例
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1)
        {
            perror("epoll_create1");
            return -1;
        }

        // 将监听套接字添加到epoll实例中
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1)
        {
            perror("epoll_ctl: listen_fd");
            return -1;
        }

        // 分配事件数组
        events = new struct epoll_event[MAX_EVENTS];

        // 事件循环
        time_t last_stats_time = time(nullptr);
        while (true)
        {
            int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1秒超时

            if (nfds == -1)
            {
                // 被信号中断，继续等待
                if (errno == EINTR)
                {
                    continue;
                }
                perror("epoll_wait");
                break;
            }

            // 处理事件
            for (int i = 0; i < nfds; i++)
            {
                if (events[i].data.fd == listen_fd)
                {
                    // 新连接
                    handleAccept();
                }
                else
                {
                    // 客户端数据
                    handleClient(events[i].data.fd);
                }
            }

            // 每10秒打印一次统计信息
            time_t now = time(nullptr);
            if (difftime(now, last_stats_time) >= 1)
            {
                printStats();
                last_stats_time = now;
            }
        }

        return 0;
    }
};

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;

    if (argc > 1)
    {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535)
        {
            std::cerr << "Invalid port number" << std::endl;
            return 1;
        }
    }

    EchoServer server(port);
    return server.start();
}
