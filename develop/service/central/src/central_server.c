#include "../include/central_server.h"
#include "../include/thread_pool.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* ========== 内部状态 ========== */

#define MAX_EVENTS 64

static int g_server_sock = -1;
static int g_epoll_fd = -1;
static volatile int g_running = 0;

// 客户端连接池
static ClientConnection g_clients[MAX_CLIENTS];

// 回调函数
static on_data_received_t g_on_data = NULL;
static on_device_online_t g_on_online = NULL;
static on_device_offline_t g_on_offline = NULL;
static on_heartbeat_t g_on_heartbeat = NULL;

// 解析函数声明（来自 protocol_parser.c，声明在 proto.h）
// parse_packet(char *buf, int *len, uint16_t *out_device_id, DataPayload *out_payload)

/* ========== 内部辅助函数 ========== */

// 设置非阻塞
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 查找客户端
static ClientConnection* find_client_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd == fd) {
            return &g_clients[i];
        }
    }
    return NULL;
}

// 查找空闲客户端槽位
static ClientConnection* find_free_client(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd == -1) {
            return &g_clients[i];
        }
    }
    return NULL;
}

// 重置客户端
static void reset_client(ClientConnection *conn) {
    if (!conn) return;
    conn->fd = -1;
    conn->device_id = 0;
    conn->recv_len = 0;
    conn->online = 0;
    conn->last_heartbeat = 0;
    conn->connected_time = 0;
    conn->packet_count = 0;
    memset(conn->ip, 0, sizeof(conn->ip));
    conn->port = 0;
    memset(conn->recv_buf, 0, sizeof(conn->recv_buf));
}

/* ========== 客户端管理 ========== */

// 添加客户端
static void add_client(int fd, struct sockaddr_in *addr) {
    ClientConnection *conn = find_free_client();
    if (!conn) {
        printf("[WARN] 达到最大连接数 %d，拒绝新连接\n", MAX_CLIENTS);
        close(fd);
        return;
    }
    
    reset_client(conn);
    conn->fd = fd;
    conn->online = 1;
    conn->last_heartbeat = time(NULL);
    conn->connected_time = time(NULL);
    
    inet_ntop(AF_INET, &addr->sin_addr, conn->ip, sizeof(conn->ip));
    conn->port = ntohs(addr->sin_port);
    
    // 加入epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // 边沿触发
    ev.data.fd = fd;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl add");
        close(fd);
        reset_client(conn);
        return;
    }
    
    printf("[INFO] 新连接: fd=%d, ip=%s, port=%d\n", fd, conn->ip, conn->port);
}

// 移除客户端
static void remove_client(int fd) {
    ClientConnection *conn = find_client_by_fd(fd);
    if (!conn) {
        close(fd);
        return;
    }
    
    if (conn->online && g_on_offline) {
        g_on_offline(conn);
    }
    
    printf("[INFO] 断开连接: fd=%d, device_id=%d, ip=%s\n", 
           fd, conn->device_id, conn->ip);
    
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    reset_client(conn);
}

/* ========== 数据接收与解析 ========== */

// 处理客户端数据
static void handle_client_data(int fd) {
    ClientConnection *conn = find_client_by_fd(fd);
    if (!conn) return;

    // 【边沿触发】必须循环 recv 直到 EAGAIN/EWOULDBLOCK，否则未读数据
    // 不会再触发事件，导致粘包/大包解析卡死或丢数据。
    while (1) {
        if (conn->recv_len >= (int)sizeof(conn->recv_buf) - 1) {
            printf("[WARN] fd=%d 接收缓冲区满，丢弃旧数据\n", fd);
            conn->recv_len = 0;
        }
        int n = recv(fd,
                     conn->recv_buf + conn->recv_len,
                     sizeof(conn->recv_buf) - conn->recv_len - 1, 0);
        if (n > 0) {
            conn->recv_len += n;
            continue;  // 继续排空，直到返回 EAGAIN
        }
        if (n == 0) {
            printf("[INFO] 客户端正常关闭: fd=%d\n", fd);
            remove_client(fd);
            return;
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;  // 内核缓冲已排空
        }
        if (errno == EINTR) {
            continue;
        }
        perror("recv");
        remove_client(fd);
        return;
    }

    // 循环解析数据包
    DataPayload payload;
    ParseResult result;

    while (1) {
        uint16_t new_id = 0;
        result = parse_packet(conn->recv_buf, &conn->recv_len, &new_id, &payload);

        if (result == PARSE_OK) {
            conn->packet_count++;
            conn->last_heartbeat = time(NULL);

            // 设备ID变更检测（device_id 在包头，由解析器返回）
            if (new_id != 0) {
                if (conn->device_id != 0 && conn->device_id != new_id) {
                    printf("[WARN] 设备ID变更: %d -> %d\n", conn->device_id, new_id);
                }
                conn->device_id = new_id;
            }

            // 首次收到有效包/从离线恢复 -> 触发上线回调
            if (!conn->online) {
                conn->online = 1;
                if (g_on_online) g_on_online(conn);
            }

            thread_pool_submit(conn, &payload);

        } else if (result == PARSE_HEARTBEAT) {
            conn->last_heartbeat = time(NULL);

            if (new_id != 0) {
                if (conn->device_id != 0 && conn->device_id != new_id) {
                    printf("[WARN] 设备ID变更: %d -> %d\n", conn->device_id, new_id);
                }
                conn->device_id = new_id;
            }

            if (!conn->online) {
                conn->online = 1;
                if (g_on_online) g_on_online(conn);
            }

            if (g_on_heartbeat) g_on_heartbeat(conn);

        } else if (result == PARSE_INCOMPLETE) {
            break;

        } else if (result == PARSE_CRC_ERROR) {
            printf("[WARN] fd=%d CRC校验失败，已丢弃坏包\n", fd);
            continue;

        } else if (result == PARSE_MAGIC_ERROR) {
            printf("[WARN] fd=%d 魔数错误，已尝试恢复\n", fd);
            continue;

        } else {
            printf("[WARN] fd=%d 未知错误: %d\n", fd, result);
            continue;
        }
    }
}

// 心跳超时检查
static void check_heartbeat_timeout(void) {
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        ClientConnection *conn = &g_clients[i];
        if (conn->fd == -1 || !conn->online) continue;

        if (now - conn->last_heartbeat > HEARTBEAT_TIMEOUT) {
            printf("[WARN] 设备心跳超时: fd=%d, device_id=%d, ip=%s\n",
                   conn->fd, conn->device_id, conn->ip);
            // 直接断开：释放连接槽位，设备需重连。
            // remove_client 会在 online==1 时触发 g_on_offline 回调。
            remove_client(conn->fd);
        }
    }
}

/* ========== 创建服务端Socket ========== */

static int create_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // 地址重用
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sock);
        return -1;
    }
    
    // 非阻塞
    if (set_nonblocking(sock) < 0) {
        perror("fcntl");
        close(sock);
        return -1;
    }
    
    // 绑定
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }
    
    // 监听
    if (listen(sock, 128) < 0) {
        perror("listen");
        close(sock);
        return -1;
    }
    
    printf("[INFO] 服务启动成功，监听端口: %d\n", port);
    return sock;
}

/* ========== 信号处理 ========== */

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[INFO] 收到停止信号，正在关闭服务...\n");
        g_running = 0;
    }
}

/* ========== 对外接口实现 ========== */

int server_init(int port, 
                on_data_received_t on_data,
                on_device_online_t on_online,
                on_device_offline_t on_offline,
                on_heartbeat_t on_heartbeat) {
    
    // 保存回调
    g_on_data = on_data;
    g_on_online = on_online;
    g_on_offline = on_offline;
    g_on_heartbeat = on_heartbeat;
    
    // 初始化客户端池
    for (int i = 0; i < MAX_CLIENTS; i++) {
        reset_client(&g_clients[i]);
    }
    
    // 创建epoll
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) {
        perror("epoll_create1");
        return -1;
    }
    
    // 创建服务端socket
    g_server_sock = create_server_socket(port);
    if (g_server_sock < 0) {
        close(g_epoll_fd);
        return -1;
    }
    
    // 服务端socket加入epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = g_server_sock;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_server_sock, &ev) < 0) {
        perror("epoll_ctl");
        close(g_server_sock);
        close(g_epoll_fd);
        return -1;
    }
    
    // 注册信号
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE
    
    g_running = 1;
    return 0;
}

void server_run(void) {
    struct epoll_event events[MAX_EVENTS];
    time_t last_check = 0;
    
    printf("[INFO] 事件循环启动\n");
    
    while (g_running) {
        // 等待事件，超时1秒
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EVENTS, 1000);
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                // 连接异常
                if (fd != g_server_sock) {
                    remove_client(fd);
                }
                continue;
            }
            
            if (fd == g_server_sock) {
                // 新连接
                while (1) {
                    struct sockaddr_in addr;
                    socklen_t addrlen = sizeof(addr);
                    int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
                    
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // 没有更多连接
                        }
                        perror("accept");
                        break;
                    }
                    
                    // 设置非阻塞、TCP_NODELAY
                    set_nonblocking(client_fd);
                    int flag = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                    
                    add_client(client_fd, &addr);
                }
                
            } else {
                // 客户端数据
                handle_client_data(fd);
            }
        }
        
        // 每秒检查心跳超时
        time_t now = time(NULL);
        if (now - last_check >= 1) {
            check_heartbeat_timeout();
            last_check = now;
        }
    }
    
    printf("[INFO] 事件循环退出\n");
}

void server_stop(void) {
    g_running = 0;
    
    // 关闭所有客户端
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1) {
            close(g_clients[i].fd);
            reset_client(&g_clients[i]);
        }
    }
    
    // 关闭服务端socket
    if (g_server_sock != -1) {
        close(g_server_sock);
        g_server_sock = -1;
    }
    
    // 关闭epoll
    if (g_epoll_fd != -1) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
    
    printf("[INFO] 服务已停止\n");
}

int server_get_client_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1 && g_clients[i].online) {
            count++;
        }
    }
    return count;
}

ClientConnection* server_find_by_device_id(uint16_t device_id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1 && 
            g_clients[i].online && 
            g_clients[i].device_id == device_id) {
            return &g_clients[i];
        }
    }
    return NULL;
}

void server_disconnect_client(int fd) {
    remove_client(fd);
}