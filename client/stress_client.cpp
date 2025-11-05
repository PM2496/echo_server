#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <chrono>
#include <vector>
#include <algorithm>

#define BUFFER_SIZE 4096
#define DEFAULT_PORT 8888
#define DEFAULT_MESSAGE_SIZE 1024
#define DEFAULT_MESSAGE_COUNT 10000

class StressClient
{
private:
    std::string server_ip;
    int port;
    int message_size;
    int message_count;

    // 统计数据
    std::vector<double> latencies;
    unsigned long long total_bytes_sent;
    unsigned long long total_bytes_received;
    int successful_messages;
    int failed_messages;

    // 连接到服务器
    int connectToServer()
    {
        /**
         * AF_INET: IPv4协议
         * SOCK_STREAM: 提供有序、可靠、双向、基于连接的字节流。
         * 0: 给定套接字类型的默认协议
         **/
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1)
        {
            perror("socket");
            return -1;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        // 转换IP地址
        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0)
        {
            std::cerr << "Invalid address: " << server_ip << std::endl;
            close(sock_fd);
            return -1;
        }

        if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        {
            perror("connect");
            close(sock_fd);
            return -1;
        }

        return sock_fd;
    }

    // 发送并接收数据
    bool sendAndReceive(int sock_fd, const char *message, int msg_len, double &latency)
    {
        auto start = std::chrono::high_resolution_clock::now();

        // 发送消息
        ssize_t sent = 0;
        while (sent < msg_len)
        {
            ssize_t n = write(sock_fd, message + sent, msg_len - sent);
            if (n <= 0)
            {
                perror("write");
                return false;
            }
            sent += n;
        }
        total_bytes_sent += sent;

        // 接收回显
        char buffer[BUFFER_SIZE];
        ssize_t received = 0;
        while (received < msg_len)
        {
            ssize_t n = read(sock_fd, buffer + received, msg_len - received);
            if (n <= 0)
            {
                if (n == 0)
                {
                    std::cerr << "Server closed connection" << std::endl;
                }
                else
                {
                    perror("read");
                }
                return false;
            }
            received += n;
        }
        total_bytes_received += received;

        auto end = std::chrono::high_resolution_clock::now();
        latency = std::chrono::duration<double, std::milli>(end - start).count();

        // 验证回显数据是否正确
        if (memcmp(message, buffer, msg_len) != 0)
        {
            std::cerr << "Echo mismatch!" << std::endl;
            return false;
        }

        return true;
    }

    // 计算并打印统计信息
    void calculateStats(double total_time)
    {
        std::cout << "\n========== Stress Test Results ==========" << std::endl;
        std::cout << "Server: " << server_ip << ":" << port << std::endl;
        std::cout << "Message size: " << message_size << " bytes" << std::endl;
        std::cout << "Total messages: " << message_count << std::endl;
        std::cout << "Successful: " << successful_messages << std::endl;
        std::cout << "Failed: " << failed_messages << std::endl;
        std::cout << "Total time: " << total_time << " seconds" << std::endl;

        if (successful_messages > 0)
        {
            // 计算延迟统计数据
            std::sort(latencies.begin(), latencies.end());

            double sum = 0;
            for (double lat : latencies)
            {
                sum += lat;
            }
            double avg_latency = sum / latencies.size();
            double min_latency = latencies.front();
            double max_latency = latencies.back();
            double p50_latency = latencies[latencies.size() * 50 / 100];
            double p95_latency = latencies[latencies.size() * 95 / 100];
            double p99_latency = latencies[latencies.size() * 99 / 100];

            std::cout << "\n--- Latency Statistics (ms) ---" << std::endl;
            std::cout << "Min:     " << min_latency << std::endl;
            std::cout << "Average: " << avg_latency << std::endl;
            std::cout << "P50:     " << p50_latency << std::endl;
            std::cout << "P95:     " << p95_latency << std::endl;
            std::cout << "P99:     " << p99_latency << std::endl;
            std::cout << "Max:     " << max_latency << std::endl;

            std::cout << "\n--- Throughput ---" << std::endl;
            std::cout << "Messages/sec: " << (successful_messages / total_time) << std::endl;
            std::cout << "Sent:     " << (total_bytes_sent / total_time / 1024.0) << " KB/s" << std::endl;
            std::cout << "Received: " << (total_bytes_received / total_time / 1024.0) << " KB/s" << std::endl;
        }

        std::cout << "========================================\n"
                  << std::endl;
    }

public:
    StressClient(const std::string &ip, int p, int msg_size, int msg_count)
        : server_ip(ip), port(p), message_size(msg_size), message_count(msg_count),
          total_bytes_sent(0), total_bytes_received(0),
          successful_messages(0), failed_messages(0)
    {
        latencies.reserve(msg_count);
    }

    int run()
    {
        std::cout << "Connecting to server " << server_ip << ":" << port << "..." << std::endl;

        int sock_fd = connectToServer();
        if (sock_fd == -1)
        {
            return -1;
        }

        std::cout << "Connected! Starting stress test..." << std::endl;
        std::cout << "Sending " << message_count << " messages of " << message_size << " bytes each (stop-and-wait mode)\n"
                  << std::endl;

        // 准备消息数据
        char *message = new char[message_size];
        for (int i = 0; i < message_size; i++)
        {
            message[i] = 'A' + (i % 26);
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        // 逐个发送消息（停止-等待模式）
        for (int i = 0; i < message_count; i++)
        {
            double latency;

            if (sendAndReceive(sock_fd, message, message_size, latency))
            {
                successful_messages++;
                latencies.push_back(latency);
            }
            else
            {
                failed_messages++;
                std::cerr << "Message " << (i + 1) << " failed" << std::endl;
            }

            // 进度显示
            // if ((i + 1) % 1000 == 0)
            // {
            //     std::cout << "Progress: " << (i + 1) << "/" << message_count << " messages sent" << std::endl;
            // }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration<double>(end_time - start_time).count();

        delete[] message;
        close(sock_fd);

        // 计算并打印统计信息
        calculateStats(total_time);

        return (failed_messages == 0) ? 0 : 1;
    }
};

void printUsage(const char *prog_name)
{
    std::cout << "Usage: " << prog_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h <host>       Server IP address (default: 127.0.0.1)" << std::endl;
    std::cout << "  -p <port>       Server port (default: 8888)" << std::endl;
    std::cout << "  -s <size>       Message size in bytes (default: 1024)" << std::endl;
    std::cout << "  -n <count>      Number of messages to send (default: 10000)" << std::endl;
    std::cout << "  -?              Show this help message" << std::endl;
}

int main(int argc, char *argv[])
{
    std::string server_ip = "127.0.0.1";
    int port = DEFAULT_PORT;
    int message_size = DEFAULT_MESSAGE_SIZE;
    int message_count = DEFAULT_MESSAGE_COUNT;

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
        {
            server_ip = argv[++i];
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
        {
            message_size = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            message_count = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-?") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (port <= 0 || port > 65535)
    {
        std::cerr << "Invalid port number" << std::endl;
        return 1;
    }

    if (message_size <= 0 || message_size > BUFFER_SIZE)
    {
        std::cerr << "Invalid message size (must be 1-" << BUFFER_SIZE << ")" << std::endl;
        return 1;
    }

    if (message_count <= 0)
    {
        std::cerr << "Invalid message count" << std::endl;
        return 1;
    }

    StressClient client(server_ip, port, message_size, message_count);
    return client.run();
}
