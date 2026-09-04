/* ============================================================================
 * central_server.c — 高并发中央服务器框架（基于 epoll 边沿触发）
 * ============================================================================
 * 【职责】
 *  1. TCP 服务端：创建监听 socket → accept 新连接 → recv 数据
 *  2. 基于 epoll ET（边沿触发）+ 非阻塞 I/O 实现高并发事件循环
 *  3. 管理最多 MAX_CLIENTS(100) 个客户端连接池
 *  4. 调用 protocol_parser.c 解析协议，将结果提交给 thread_pool.c 处理
 *
 * 【架构位置】
 *  server_main.c（业务入口）
 *       ↓ server_init / server_run
 *  central_server.c（本文件，网络层）
 *       ↓ thread_pool_submit
 *  thread_pool.c（并发层）
 *       ↓ 回调
 *  server_main.c 的 on_data_received → 入库
 *
 * 【epoll 边沿触发(ET)要点】
 *  - ET 模式下，当有新数据到达时只通知一次，必须一次性读完（循环 recv 直到 EAGAIN）
 *  - 如果没读完，内核不会再触发该 fd 的 EPOLLIN 事件，导致数据"卡死"
 *  - accept 也必须循环调用直到 EAGAIN，否则新连接会丢失
 *
 * 【心跳超时机制】
 *  - 每个连接维护 last_heartbeat 时间戳
 *  - 事件循环每秒检查一次，超过 HEARTBEAT_TIMEOUT(15秒) 未收到包则断开
 *  - 断开时触发 on_offline 回调，由上层更新数据库设备状态
 * ============================================================================
 */

/* ========== 头文件 ========== */
#include "../include/central_server.h"   // ClientConnection、回调类型、MAX_CLIENTS 等宏定义
#include "../include/thread_pool.h"      // thread_pool_submit：将数据提交到工作线程队列
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // close()
#include <fcntl.h>       // fcntl()：设置非阻塞
#include <errno.h>       // errno：判断 EAGAIN/EWOULDBLOCK/EINTR
#include <signal.h>      // signal()：注册信号处理函数
#include <sys/epoll.h>   // epoll_create1 / epoll_ctl / epoll_wait
#include <sys/socket.h>  // socket / bind / listen / accept / recv / setsockopt
#include <netinet/in.h>  // struct sockaddr_in / htons / htonl
#include <netinet/tcp.h> // TCP_NODELAY
#include <arpa/inet.h>   // inet_ntop / ntohs / ntohl

/* ============================================================================
 * 一、内部全局状态
 * ============================================================================ */

#define MAX_EVENTS 64  // epoll_wait 一次最多返回的事件数

/* ---------- 全局文件描述符 ---------- */
static int g_server_sock = -1;   // 监听 socket 的 fd，-1 表示未创建
static int g_epoll_fd = -1;      // epoll 实例的 fd，-1 表示未创建
static volatile int g_running = 0; // 事件循环运行标志：1=运行中，0=收到停止信号

/* ---------- 客户端连接池 ---------- */
// 用固定大小数组管理所有客户端连接，避免动态内存分配
// 索引即槽位号，fd==-1 表示空闲槽位
static ClientConnection g_clients[MAX_CLIENTS];

/* ---------- 上层注册的回调函数 ---------- */
// 由 server_main.c 在 server_init 时传入，事件循环中在适当时机调用
static on_data_received_t g_on_data = NULL;       // 收到数据包时回调
static on_device_online_t g_on_online = NULL;     // 设备上线时回调
static on_device_offline_t g_on_offline = NULL;   // 设备离线时回调
static on_heartbeat_t g_on_heartbeat = NULL;      // 收到心跳包时回调

// parse_packet() 声明在 proto.h 中，实现见 protocol_parser.c
// 负责：魔数校验 → CRC校验 → 解析包头 → 提取 DataPayload
// parse_packet(char *buf, int *len, uint16_t *out_device_id, DataPayload *out_payload)

/* ============================================================================
 * 二、内部辅助函数
 *    set_nonblocking — 设置 fd 为非阻塞模式（epoll ET 必须配合非阻塞 I/O）
 *    find_client_by_fd — 根据 fd 查找对应的连接上下文
 *    find_free_client — 在连接池中找到一个空闲槽位
 *    reset_client — 将连接槽位重置为初始状态（fd=-1 表示空闲）
 * ============================================================================ */

/**
 * @brief 将文件描述符设置为非阻塞模式
 * @param fd  待设置的 fd（socket）
 * @return 0 成功，-1 失败
 *
 * 原理：fcntl 获取当前 flags → 追加 O_NONBLOCK → fcntl 写回
 * 为什么必须非阻塞：epoll ET 模式下，如果 recv/accept 是阻塞的，
 * 一旦读到末尾会阻塞在那，无法处理其他事件，整个服务器卡死。
 */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 根据 fd 在连接池中查找对应的 ClientConnection
 * @param fd  客户端的 socket fd
 * @return 找到返回指针，未找到返回 NULL
 *
 * 线性扫描，最多遍历 MAX_CLIENTS(100) 个槽位。
 * 在连接数不多的场景下足够快；如果需要优化可改为哈希表。
 */
static ClientConnection* find_client_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd == fd) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief 在连接池中找到一个空闲槽位（fd == -1）
 * @return 空闲槽位指针，池满返回 NULL
 *
 * 用于新连接到来时分配槽位。
 */
static ClientConnection* find_free_client(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd == -1) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief 将连接槽位重置为初始状态
 * @param conn  待重置的连接指针
 *
 * fd=-1 标记槽位空闲，所有计数器和缓冲区清零。
 * 在 remove_client 和连接池初始化时调用。
 */
static void reset_client(ClientConnection *conn) {
    if (!conn) return;
    conn->fd = -1;
    conn->device_id = 0;
    conn->recv_len = 0;
    conn->online = 0;
    conn->is_hmi = 0;
    conn->last_heartbeat = 0;
    conn->connected_time = 0;
    conn->packet_count = 0;
    memset(conn->ip, 0, sizeof(conn->ip));
    conn->port = 0;
    memset(conn->recv_buf, 0, sizeof(conn->recv_buf));
}

/* ============================================================================
 * 三、客户端连接管理
 *    add_client  — 接受新连接，分配槽位，注册到 epoll
 *    remove_client — 断开连接，触发离线回调，释放槽位
 * ============================================================================ */

/**
 * @brief 接受一个新客户端连接并注册到 epoll
 * @param fd    accept 返回的客户端 socket fd
 * @param addr  客户端的地址信息（IP + 端口）
 *
 * 流程：
 *  1. find_free_client() 分配槽位 → 满则拒绝 close(fd)
 *  2. reset_client() 清空槽位 → 填入 fd/IP/端口/时间戳
 *  3. epoll_ctl(ADD) 将 fd 加入 epoll 监听，使用边沿触发(EPOLLET)
 *  4. 注意：新连接的 online=1，但此时不会触发 on_online 回调
 *     （回调在首次收到有效数据包或心跳时才触发，见 handle_client_data）
 */
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
    
    // 将二进制 IP 转为点分十进制字符串（如 "192.168.1.100"）
    inet_ntop(AF_INET, &addr->sin_addr, conn->ip, sizeof(conn->ip));
    conn->port = ntohs(addr->sin_port);  // 网络字节序 → 主机字节序
    
    // 将新 fd 注册到 epoll，EPOLLET = 边沿触发模式
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // 监听可读事件 + 边沿触发
    ev.data.fd = fd;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl add");
        close(fd);
        reset_client(conn);
        return;
    }
    
    printf("[INFO] 新连接: fd=%d, ip=%s, port=%d\n", fd, conn->ip, conn->port);
}

/**
 * @brief 断开一个客户端连接并释放槽位
 * @param fd  要断开的客户端 socket fd
 *
 * 流程：
 *  1. 如果连接处于在线状态（online==1），先触发 on_offline 回调
 *     → server_main.c 会调用 db_update_device_status(device_id, 0) 标记设备离线
 *  2. epoll_ctl(DEL) 从 epoll 中移除 fd
 *  3. close(fd) 关闭 socket
 *  4. reset_client() 清空槽位，fd=-1 标记为空闲
 *
 * 触发时机：心跳超时、客户端主动断开、recv 错误
 */
static void remove_client(int fd) {
    ClientConnection *conn = find_client_by_fd(fd);
    if (!conn) {
        close(fd);
        return;
    }
    
    // 设备在线时触发离线回调（更新数据库状态）
    if (conn->online && g_on_offline) {
        g_on_offline(conn);
    }
    
    printf("[INFO] 断开连接: fd=%d, device_id=%d, ip=%s\n", 
           fd, conn->device_id, conn->ip);
    
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);  // 从 epoll 移除
    close(fd);        // 关闭 socket，释放文件描述符
    reset_client(conn);  // 归还槽位到连接池
}

/* ============================================================================
 * 四、数据接收与协议解析
 *    handle_client_data   — 核心：读取数据 → 解析协议 → 提交线程池
 *    check_heartbeat_timeout — 定期检查超时，断开失联设备
 * ============================================================================
 * 数据处理流程：
 *  1. epoll 通知某 fd 可读 → 调用 handle_client_data
 *  2. 循环 recv 直到 EAGAIN（排空内核缓冲区）
 *  3. 循环 parse_packet 解析数据包（可能缓冲区里有多个包/粘包）
 *  4. 解析成功：更新 device_id → 检测上线 → thread_pool_submit 提交任务
 *  5. 心跳包：更新时间戳 → 触发心跳回调
 *  6. 数据不完整：退出循环，等待下次 recv 补齐
 * ============================================================================ */

/**
 * @brief 处理客户端发来的数据（边沿触发，必须一次性读完）
 * @param fd  触发可读事件的客户端 fd
 *
 * 两个循环：
 *  外层 while(1) — recv 循环：排空内核缓冲区直到 EAGAIN
 *  内层 while(1) — parse_packet 循环：从 recv_buf 中逐个解析数据包
 *
 * 边沿触发的关键点：
 *  - 内核只在数据"新到达"时通知一次
 *  - 如果这次没读完，下次不会再来通知，数据就"卡"在缓冲区了
 *  - 所以必须循环 recv，直到返回 EAGAIN（表示缓冲区已空）
 */
static void handle_client_data(int fd) {
    ClientConnection *conn = find_client_by_fd(fd);
    if (!conn) return;

    /* ---------- 外层循环：recv 直到 EAGAIN ---------- */
    while (1) {
        // 防止接收缓冲区溢出：如果已经快满了，先清空（生产环境应丢弃旧数据或断开）
        if (conn->recv_len >= (int)sizeof(conn->recv_buf) - 1) {
            printf("[WARN] fd=%d 接收缓冲区满，丢弃旧数据\n", fd);
            conn->recv_len = 0;
        }
        // recv：从 fd 读取数据，追加到 recv_buf 末尾
        int n = recv(fd,
                     conn->recv_buf + conn->recv_len,     // 写入位置：缓冲区当前有效数据之后
                     sizeof(conn->recv_buf) - conn->recv_len - 1,  // 可写空间（留1字节给 \0）
                     0);
        if (n > 0) {
            conn->recv_len += n;
            continue;  // 继续排空，直到返回 EAGAIN
        }
        if (n == 0) {
            // recv 返回 0 表示对端主动关闭连接（FIN）
            printf("[INFO] 客户端正常关闭: fd=%d\n", fd);
            remove_client(fd);
            return;
        }
        // n < 0：出错，判断具体原因
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;  // 内核缓冲区已读空，退出 recv 循环，开始解析
        }
        if (errno == EINTR) {
            continue;  // 被信号中断，重试 recv
        }
        // 其他真实错误，断开连接
        perror("recv");
        remove_client(fd);
        return;
    }

    /* ---------- 内层循环：从 recv_buf 中逐个解析数据包 ---------- */
    // 可能一次 recv 收到了多个包（粘包），需要循环解析
    // 也可能收到的是半个包（PARSE_INCOMPLETE），等待下次 recv 补齐
    DataPayload payload;
    ParseResult result;

    while (1) {
        uint16_t new_id = 0;
        result = parse_packet(conn->recv_buf, &conn->recv_len, &new_id, &payload);
        // parse_packet 会消耗已解析的字节（recv_buf 前移，recv_len 减少）

        if (result == PARSE_OK) {
            /* --- 收到 type=0x01 数据上报包 --- */
            conn->packet_count++;
            conn->last_heartbeat = time(NULL);  // 任何有效包都刷新心跳时间

            // device_id 变更检测：同一连接可能先上报设备1，后切换为设备2
            if (new_id != 0) {
                if (conn->device_id != 0 && conn->device_id != new_id) {
                    printf("[WARN] 设备ID变更: %d -> %d\n", conn->device_id, new_id);
                }
                conn->device_id = new_id;
            }

            // 首次收到有效包 / 从离线恢复 → 触发上线回调
            // online 字段在 add_client 时已置1，这里处理的是"重新上线"的情况
            if (!conn->online) {
                conn->online = 1;
                if (g_on_online) g_on_online(conn);
            }

            // 将数据提交到线程池队列，由工作线程异步入库
            // 注意：这里传递的是 conn 和 payload 的指针，队列内部会做 memcpy 复制
            thread_pool_submit(conn, &payload);

        } else if (result == PARSE_HEARTBEAT) {
            /* --- 收到 type=0x02 心跳包（无负载） --- */
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

        } else if (result == PARSE_REGISTER) {
            /* --- 收到 type=0x03 HMI注册包 --- */
            conn->is_hmi = 1;
            conn->last_heartbeat = time(NULL);
            printf("[INFO] HMI看板注册: fd=%d, ip=%s\n", fd, conn->ip);

            /* 触发上线回调，向HMI推送设备列表和实时数据 */
            conn->online = 0;  /* 临时置0，让 on_device_online 能触发 */
            if (g_on_online) g_on_online(conn);

        } else if (result == PARSE_INCOMPLETE) {
            /* --- 数据不完整：包头还没收全，或负载只收到一半 --- */
            // 退出内层循环，等下次 epoll 通知可读时继续 recv 补齐
            break;

        } else if (result == PARSE_CRC_ERROR) {
            /* --- CRC 校验失败：数据在传输中损坏 --- */
            printf("[WARN] fd=%d CRC校验失败，已丢弃坏包\n", fd);
            continue;  // 丢弃坏包，尝试解析下一个包

        } else if (result == PARSE_MAGIC_ERROR) {
            /* --- 魔数错误：可能因为丢包导致字节流错位 --- */
            // parse_packet 内部已跳过1字节尝试恢复，这里继续解析
            printf("[WARN] fd=%d 魔数错误，已尝试恢复\n", fd);
            continue;

        } else {
            /* --- 未知错误 --- */
            printf("[WARN] fd=%d 未知错误: %d\n", fd, result);
            continue;
        }
    }
}

/**
 * @brief 心跳超时检查——遍历所有在线连接，断开超过15秒未收到包的设备
 *
 * 调用时机：server_run 的事件循环中，每秒执行一次
 * 超时逻辑：now - last_heartbeat > HEARTBEAT_TIMEOUT(15秒) → 断开
 * 断开后：设备需要重新建立 TCP 连接才能恢复上线
 */
static void check_heartbeat_timeout(void) {
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        ClientConnection *conn = &g_clients[i];
        // 跳过空闲槽位（fd==-1）和离线设备
        if (conn->fd == -1 || !conn->online) continue;

        if (now - conn->last_heartbeat > HEARTBEAT_TIMEOUT) {
            printf("[WARN] 设备心跳超时: fd=%d, device_id=%d, ip=%s\n",
                   conn->fd, conn->device_id, conn->ip);
            // remove_client 内部会判断 online==1 并触发 g_on_offline 回调
            remove_client(conn->fd);
        }
    }
}

/* ============================================================================
 * 五、服务端 Socket 创建
 *    create_server_socket — 创建 TCP 监听 socket（非阻塞 + 地址重用）
 * ============================================================================ */

/**
 * @brief 创建 TCP 服务端 socket 并开始监听
 * @param port  监听端口号（如 8888）
 * @return 成功返回 socket fd，失败返回 -1
 *
 * 步骤：
 *  1. socket(AF_INET, SOCK_STREAM) — 创建 TCP 套接字
 *  2. setsockopt(SO_REUSEADDR) — 允许端口重用（避免服务器重启时 "Address already in use"）
 *  3. fcntl(O_NONBLOCK) — 设为非阻塞（epoll ET 必须）
 *  4. bind(INADDR_ANY, port) — 绑定所有网卡的指定端口
 *  5. listen(128) — 开始监听，backlog=128（同时允许128个未完成三次握手的连接排队）
 */
static int create_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // SO_REUSEADDR：服务器重启时允许立即复用端口，无需等待 TIME_WAIT 超时
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sock);
        return -1;
    }
    
    // 设置非阻塞：accept/recv 不会阻塞，配合 epoll ET 使用
    if (set_nonblocking(sock) < 0) {
        perror("fcntl");
        close(sock);
        return -1;
    }
    
    // 绑定地址：INADDR_ANY 表示监听所有网卡（0.0.0.0）
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);            // 主机字节序 → 网络字节序
    addr.sin_addr.s_addr = INADDR_ANY;     // 绑定所有本地接口
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }
    
    // 开始监听，backlog=128：内核为此 socket 排队的最大连接数
    if (listen(sock, 128) < 0) {
        perror("listen");
        close(sock);
        return -1;
    }
    
    printf("[INFO] 服务启动成功，监听端口: %d\n", port);
    return sock;
}

/* ============================================================================
 * 六、信号处理
 *    signal_handler — 处理 SIGINT(Ctrl+C) / SIGTERM，优雅退出事件循环
 * ============================================================================ */

/**
 * @brief 信号处理函数：收到 SIGINT 或 SIGTERM 时设置退出标志
 * @param sig  信号编号
 *
 * g_running 是 volatile 的，修改后事件循环会在下次循环时检测到并退出。
 * 注意：signal handler 中只能安全调用"异步信号安全"的函数，
 * 这里仅设置标志位，不执行任何 I/O 操作。
 */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[INFO] 收到停止信号，正在关闭服务...\n");
        g_running = 0;
    }
}

/* ============================================================================
 * 七、对外接口实现
 *    server_init  — 初始化：保存回调 → 初始化连接池 → 创建 epoll → 创建 socket → 注册信号
 *    server_run   — 运行事件循环（阻塞，直到 g_running=0）
 *    server_stop  — 优雅退出：关闭所有连接 → 关闭 socket → 关闭 epoll
 *    server_get_client_count — 返回当前在线连接数
 *    server_find_by_device_id — 按设备号查找连接
 *    server_disconnect_client — 主动断开指定连接
 * ============================================================================ */

/**
 * @brief 初始化服务器（由 server_main.c 调用）
 * @param port        监听端口
 * @param on_data     数据上报回调
 * @param on_online   设备上线回调
 * @param on_offline  设备离线回调
 * @param on_heartbeat 心跳回调
 * @return 0 成功，-1 失败
 *
 * 执行顺序：
 *  1. 保存4个回调函数指针到全局变量
 *  2. 初始化连接池（所有槽位 fd=-1，标记为空闲）
 *  3. epoll_create1 创建 epoll 实例
 *  4. create_server_socket 创建监听 socket 并加入 epoll
 *  5. signal() 注册 SIGINT/SIGTERM 处理函数
 *  6. signal(SIGPIPE, SIG_IGN) 忽略 SIGPIPE（对端断开后写入不会导致进程崩溃）
 */
int server_init(int port, 
                on_data_received_t on_data,
                on_device_online_t on_online,
                on_device_offline_t on_offline,
                on_heartbeat_t on_heartbeat) {
    
    // 保存回调函数指针
    g_on_data = on_data;
    g_on_online = on_online;
    g_on_offline = on_offline;
    g_on_heartbeat = on_heartbeat;
    
    // 初始化连接池：所有槽位标记为空闲（fd=-1）
    for (int i = 0; i < MAX_CLIENTS; i++) {
        reset_client(&g_clients[i]);
    }
    
    // 创建 epoll 实例（参数 0 表示默认行为）
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) {
        perror("epoll_create1");
        return -1;
    }
    
    // 创建监听 socket 并加入 epoll
    g_server_sock = create_server_socket(port);
    if (g_server_sock < 0) {
        close(g_epoll_fd);
        return -1;
    }
    
    // 监听 socket 加入 epoll（水平触发，因为 accept 循环会排空）
    struct epoll_event ev;
    ev.events = EPOLLIN;       // 监听可读事件（有新连接到来时触发）
    ev.data.fd = g_server_sock;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_server_sock, &ev) < 0) {
        perror("epoll_ctl");
        close(g_server_sock);
        close(g_epoll_fd);
        return -1;
    }
    
    // 注册信号处理
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill 命令 
    signal(SIGPIPE, SIG_IGN);         // 忽略 SIGPIPE：对端断开后继续写入不会崩溃
    
    g_running = 1;
    return 0;
}

/**
 * @brief 事件循环（阻塞运行，直到收到停止信号）
 *
 * 核心逻辑：
 *  1. epoll_wait 阻塞等待事件（超时1秒，用于心跳检查）
 *  2. 遍历返回的事件列表：
 *     - EPOLLHUP/EPOLLERR → 连接异常，断开
 *     - fd == g_server_sock → 有新连接，循环 accept
 *     - 其他 fd → 有数据可读，调用 handle_client_data
 *  3. 每秒执行一次心跳超时检查
 *
 * 退出条件：g_running 被信号处理函数置为 0
 */
void server_run(void) {
    struct epoll_event events[MAX_EVENTS];
    time_t last_check = 0;  // 上次心跳检查的时间
    
    printf("[INFO] 事件循环启动\n");
    
    while (g_running) {
        // epoll_wait：阻塞等待事件，超时1000ms（1秒）
        // 超时返回后可顺便做心跳检查，不依赖额外的定时器线程
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EVENTS, 1000);
        
        // 遍历所有就绪事件
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            // 连接异常（对端崩溃、TCP RST 等）
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                if (fd != g_server_sock) {
                    remove_client(fd);
                }
                continue;
            }
            
            if (fd == g_server_sock) {
                /* --- 监听 socket 可读：有新连接到来 --- */
                // 必须循环 accept 直到 EAGAIN（ET 模式）
                while (1) {
                    struct sockaddr_in addr;
                    socklen_t addrlen = sizeof(addr);
                    int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
                    
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // 所有新连接已 accept 完毕
                        }
                        perror("accept");
                        break;
                    }
                    
                    // 新连接：设为非阻塞 + 禁用 Nagle 算法（降低延迟）
                    set_nonblocking(client_fd);
                    int flag = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                    // TCP_NODELAY：小数据包立即发送，不等凑够 MSS（适合工业实时场景）
                    
                    add_client(client_fd, &addr);
                }
                
            } else {
                /* --- 客户端 fd 可读：有数据到达 --- */
                handle_client_data(fd);
            }
        }
        
        // 每秒检查一次心跳超时（利用 epoll_wait 的 1 秒超时作为定时基准）
        time_t now = time(NULL);
        if (now - last_check >= 1) {
            check_heartbeat_timeout();
            last_check = now;
        }
    }
    
    printf("[INFO] 事件循环退出\n");
}

/**
 * @brief 停止服务器，释放所有资源
 *
 * 按顺序释放：客户端连接 → 监听 socket → epoll fd
 * 调用时机：server_main.c 在 server_run() 返回后调用
 */
void server_stop(void) {
    g_running = 0;
    
    // 遍历关闭所有客户端连接
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1) {
            close(g_clients[i].fd);
            reset_client(&g_clients[i]);
        }
    }
    
    // 关闭监听 socket
    if (g_server_sock != -1) {
        close(g_server_sock);
        g_server_sock = -1;
    }
    
    // 关闭 epoll 实例
    if (g_epoll_fd != -1) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
    
    printf("[INFO] 服务已停止\n");
}

/**
 * @brief 获取当前在线客户端数量
 * @return 在线连接数（online==1 且 fd!=-1 的槽位数）
 */
int server_get_client_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1 && g_clients[i].online) {
            count++;
        }
    }
    return count;
}

/**
 * @brief 按设备号查找在线连接
 * @param device_id  要查找的设备号（1~65535）
 * @return 找到返回 ClientConnection 指针，未找到或设备离线返回 NULL
 *
 * 用途：主动向指定设备发送数据时（如 HMI 请求某设备实时值），先查找连接
 */
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

/**
 * @brief 主动断开指定 fd 的客户端连接
 * @param fd  要断开的客户端 socket fd
 *
 * 与 remove_client 功能相同，作为对外接口暴露给上层使用。
 */
void server_disconnect_client(int fd) {
    remove_client(fd);
}

/**
 * @brief 向指定HMI客户端发送数据包
 * @param hmi  HMI客户端连接
 * @param type 包类型
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @return 0成功，-1失败
 */
int server_send_to_hmi(ClientConnection *hmi, uint8_t type, uint16_t device_id, const void *payload, uint16_t payload_len) {
    if (!hmi || hmi->fd < 0 || !hmi->is_hmi)
        return -1;

    // 组装12字节包头
    uint8_t header[sizeof(ProtocolHeader)];
    ProtocolHeader *hdr = (ProtocolHeader *)header;
    hdr->magic = 0x5A5A;
    hdr->payload_len = htonl(payload_len);
    hdr->type = type;
    hdr->version = 0x01;
    hdr->device_id = htons(device_id);

    // 计算CRC16：包头前8字节 + 负载
    uint8_t crc_buf[8 + 1024];
    memcpy(crc_buf, header, 8);
    if (payload && payload_len > 0)
        memcpy(crc_buf + 8, payload, payload_len);
    uint16_t crc = crc16_calc(crc_buf, 8 + payload_len);
    hdr->crc16 = htons(crc);

    // 发送包头
    if (send(hmi->fd, header, sizeof(header), MSG_NOSIGNAL) < 0)
        return -1;

    // 发送负载
    if (payload && payload_len > 0) {
        if (send(hmi->fd, payload, payload_len, MSG_NOSIGNAL) < 0)
            return -1;
    }

    return 0;
}

/**
 * @brief 向所有HMI客户端广播数据包
 * @param type 包类型
 * @param payload 负载数据
 * @param payload_len 负载长度
 */
void server_broadcast_to_hmi(uint8_t type, uint16_t device_id, const void *payload, uint16_t payload_len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd != -1 && g_clients[i].online && g_clients[i].is_hmi) {
            server_send_to_hmi(&g_clients[i], type, device_id, payload, payload_len);
        }
    }
}