/**
 * ============================================================================
 * main.c - 采集板客户端程序（支持远程控制）
 * ============================================================================
 * 文件名称：main.c
 * 项目：i.MX6ULL采集板，DHT11温湿度 + E18-D80NK红外产量采集，TCP上报二进制自定义协议
 * 
 * 功能说明：
 *   1. 硬件采集：DHT11温湿度、E18-D80NK红外传感器、设备运行状态
 *   2. 数据上报：通过 TCP 上报采集数据到服务端 (type=0x01)
 *   3. 心跳保活：每5秒发送心跳包 (type=0x02)
 *   4. 【修改点】远程控制：接收并执行服务端下发的控制指令 (type=0x03)
 *   5. 本地报警：温度超限时本地蜂鸣器报警
 *   6. 安全退出：捕获 Ctrl+C 信号，安全释放资源
 * 
 * 修改说明：
 *   【修改点1】添加 CtrlPayload 结构体定义 - 用于接收远程控制指令
 *   【修改点2】consumer_thread 添加接收缓冲区 - 存储接收到的数据
 *   【修改点3】consumer_thread 添加 select 非阻塞接收 - 避免阻塞数据上报
 *   【修改点4】consumer_thread 添加协议解析逻辑 - 解析魔数、CRC、报文类型
 *   【修改点5】consumer_thread 添加 type=0x03 控制指令处理 - 执行远程控制
 *   【修改点6】consumer_thread 添加控制指令执行 - 蜂鸣器开/关、复位等
 *   【修改点7】添加 CtrlPayload 结构体包含头文件引用
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/select.h>    /* 【修改点】添加 select 头文件 */
#include "gpio.h"
#include "dht11.h"

/*********************************************************************************
 * 全局配置变量：全部由 collector.conf 配置文件赋值
 * 注意：本处只做变量声明，没有写死内置默认值；运行前必须提供 collector.conf
 *********************************************************************************/
int     g_gpio_run_status;      // 设备运行状态输入引脚(sysfs编号)，杜邦线模拟按键
int     g_gpio_cnt_product;     // E18-D80NK红外产量计数输入GPIO(sysfs编号)
int     g_gpio_buzzer;          // 蜂鸣器控制输出引脚(sysfs编号)
int     g_temp_threshold;       // 温度报警阈值(℃)，大于等于该值触发蜂鸣器报警
int     g_alarm_cooldown;       // 蜂鸣器报警冷却时间(秒)，防止频繁连续报警
int     g_ring_buf_size;        // 线程安全环形缓冲区大小，缓存采集数据
uint16_t g_device_id;           // 采集设备ID，上报协议中用于区分不同采集板
char    g_server_ip[32];        // TCP服务端IP地址字符串，最大31字节
int     g_server_port;          // TCP服务端监听端口号

static int g_run_flag = 1;      // 全局程序退出标志，0代表退出

/* ============================================================================
 * 【修改点1】添加控制协议结构体定义 - 用于接收远程控制指令
 * 与服务端 proto.h 中的 CtrlPayload 保持一致
 * ============================================================================ */
/**
 * @brief CtrlPayload type=0x03 服务端下发控制报文负载
 * 
 * 控制码定义：
 *   0x01 - 打开蜂鸣器
 *   0x02 - 关闭蜂鸣器
 *   0x03 - 复位设备
 *   0x04 - 读取温湿度（触发立即上报）
 *   0x05 - 红外检测（触发立即上报）
 */
typedef struct {
    uint8_t ctrl_code;      // 控制码：0x01打开蜂鸣器 0x02关闭蜂鸣器 0x03复位
    uint8_t reserve[11];    // 保留字节，填充0（补齐到12字节，与服务端对齐）
} CtrlPayload;

/**
 * @brief CollectData_t 原始采集结构体
 * 保存采集得到的原始数据，之后打包成自定义协议发送
 * product_cnt：瞬时红外状态 1=检测到物体，0=无物体（瞬时产量标识）
 */
typedef struct
{
    time_t timestamp;       // Unix时间戳，记录采集时刻
    uint8_t temp;           // DHT11温度，单位℃
    uint8_t humi;           // DHT11湿度，单位%RH
    uint8_t dev_run;        // 设备状态：0停机，1运行
    uint32_t product_cnt;   // 红外瞬时状态：1检测物体，0无物体
} CollectData_t;

/************************ 自定义通信协议（和成员A服务器约定） ************************/
#pragma pack(push, 1)

/**
 * @brief ProtocolHeader 协议包头 12字节
 */
typedef struct {
    uint16_t magic;         // 魔数 固定0x5A5A，用于校验数据包开头
    uint32_t payload_len;   // 负载数据长度
    uint8_t type;           // 报文类型 0x01数据上报  0x02心跳包 0x03服务端下发控制包
    uint8_t version;        // 协议版本号，固定1
    uint16_t device_id;     // 采集板设备编号
    uint16_t crc16;         // Modbus CRC16校验码
} ProtocolHeader;

/**
 * @brief DataPayload 数据上报负载，type=0x01时有效，14字节
 * production字段现在传输红外瞬时状态：1有物体，0无物体
 */
typedef struct {
    uint32_t timestamp;      // Unix时间戳（网络字节序）
    int16_t  temperature;    // 温度×100，例25℃ → 2500
    uint8_t  status;         // 设备运行状态 0停机 1运行
    uint8_t  humi;           // 环境湿度
    int32_t  production;     // 红外瞬时状态：1检测物体 / 0无物体
    uint8_t  reserved[3];    // 保留字节，填0
} DataPayload;

#pragma pack(pop)

/* ============================================================================
 * CRC16 计算函数（Modbus 标准）
 * ============================================================================ */
/**
 * @brief crc16_calc Modbus标准CRC16校验算法
 * @param data 待校验数据缓冲区
 * @param len 数据长度
 * @return 计算得到CRC16值
 */
static uint16_t crc16_calc(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief pack_collect_data 将原始采集数据打包为完整协议报文
 * @param src 原始采集数据结构体指针
 * @param out_buf 输出报文缓冲区
 * @param buf_size 缓冲区总大小
 * @return 成功返回完整报文字节长度；失败返回-1
 */
static int pack_collect_data(CollectData_t *src, uint8_t *out_buf, int buf_size) {
    if (buf_size < (int)(sizeof(ProtocolHeader) + sizeof(DataPayload))) {
        return -1;
    }
    ProtocolHeader *hdr = (ProtocolHeader*)out_buf;
    DataPayload *payload = (DataPayload*)(out_buf + sizeof(ProtocolHeader));
    
    // 填充负载，全部转换网络大端字节序
    payload->timestamp = htonl((uint32_t)src->timestamp);
    payload->temperature = htons((int16_t)(src->temp * 100));
    payload->status = src->dev_run;
    payload->humi = src->humi;
    payload->production = htonl((int32_t)src->product_cnt);
    memset(payload->reserved, 0, 3);
    
    // 填充协议包头
    hdr->magic = htons(0x5A5A);
    hdr->payload_len = htonl(sizeof(DataPayload));
    hdr->type = 0x01;
    hdr->version = 1;
    hdr->device_id = htons(g_device_id);
    hdr->crc16 = 0; // 计算校验之前先置0
    
    // CRC计算范围：包头前8字节 + 完整负载
    uint8_t crc_buf[8 + sizeof(DataPayload)];
    memcpy(crc_buf, out_buf, 8);
    memcpy(crc_buf + 8, out_buf + sizeof(ProtocolHeader), sizeof(DataPayload));
    hdr->crc16 = htons(crc16_calc(crc_buf, 8 + sizeof(DataPayload)));
    
    return sizeof(ProtocolHeader) + sizeof(DataPayload);
}

/**
 * @brief build_heartbeat_packet 构建心跳包报文，type=0x02，无负载
 * @param out_buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 报文长度，失败-1
 */
static int build_heartbeat_packet(uint8_t *out_buf, int buf_size) {
    if (buf_size < (int)sizeof(ProtocolHeader)) {
        return -1;
    }
    ProtocolHeader *hdr = (ProtocolHeader*)out_buf;
    hdr->magic = htons(0x5A5A);
    hdr->payload_len = 0;
    hdr->type = 0x02;
    hdr->version = 1;
    hdr->device_id = htons(g_device_id);
    hdr->crc16 = htons(crc16_calc((uint8_t*)out_buf, 8));
    return sizeof(ProtocolHeader);
}

/************************ 协议定义结束 ************************/

CollectData_t *g_ring_buf = NULL;   // 动态分配环形缓冲区
int g_ring_wr = 0;                  // 写索引
int g_ring_rd = 0;                  // 读索引
pthread_mutex_t g_buf_mutex;        // 互斥锁，保护多线程访问缓冲区
pthread_cond_t g_buf_cond;          // 条件变量：无数据时消费者线程休眠
static time_t g_last_alarm_tm = 0;  // 记录上一次蜂鸣器报警时间

/**
 * @brief load_config_file 读取ini格式配置文件collector.conf
 * @param conf_path 配置文件路径
 * @return 0成功；-1读取失败程序直接退出
 * 修复：清除Windows文件带来的\r回车字符，支持行内#注释
 */
int load_config_file(const char *conf_path)
{
    FILE *fp = fopen(conf_path,"r");
    if(NULL == fp)
    {
        printf("[致命错误] 无法打开配置文件 %s，请确认文件存在！程序退出\n", conf_path);
        return -1;
    }
    char line_buf[256];
    int parse_ok = 0;
    while(fgets(line_buf,sizeof(line_buf),fp) != NULL)
    {
        line_buf[strcspn(line_buf,"\r\n")] = '\0';
        // 跳过注释 #开头 和空行
        char *p = line_buf;
        while(isspace((unsigned char)*p)) p++;
        if(*p == '#' || *p == '\0')
            continue;
        // 按 = 分割 key=value
        char *eq_pos = strchr(line_buf,'=');
        if(eq_pos == NULL)
            continue;
        *eq_pos = '\0';
        char *key = line_buf;
        char *val = eq_pos+1;
        // 去除key两边空格
        char *key_end = key + strlen(key)-1;
        while(key_end>key && isspace((unsigned char)*key_end)){
            *key_end = '\0';
            key_end--;
        }
        // 去除val两边空格
        while(*val && isspace((unsigned char)*val)) val++;
        // 清除\r、换行，遇到#行内注释直接截断
        char *v_p = val;
        while(*v_p)
        {
            if(*v_p == '\r' || *v_p == '\n' || *v_p == '#')
            {
                *v_p = '\0';
                break;
            }
            v_p++;
        }
        // 匹配各个配置项
        if(strcmp(key,"GPIO_RUN_STATUS") == 0){
            g_gpio_run_status = atoi(val); parse_ok=1;
        }else if(strcmp(key,"GPIO_CNT_PRODUCT") == 0){
            g_gpio_cnt_product = atoi(val); parse_ok=1;
        }else if(strcmp(key,"BUZZER_GPIO") == 0){
            g_gpio_buzzer = atoi(val); parse_ok=1;
        }else if(strcmp(key,"TEMP_THRESHOLD") == 0){
            g_temp_threshold = atoi(val); parse_ok=1;
        }else if(strcmp(key,"ALARM_COOLDOWN") == 0){
            g_alarm_cooldown = atoi(val); parse_ok=1;
        }else if(strcmp(key,"RING_BUF_SIZE") == 0){
            g_ring_buf_size = atoi(val); parse_ok=1;
        }else if(strcmp(key,"DEVICE_ID") == 0){
            g_device_id = (uint16_t)atoi(val); parse_ok=1;
        }else if(strcmp(key,"SERVER_IP") == 0){
            strncpy(g_server_ip,val,sizeof(g_server_ip)-1);
            g_server_ip[sizeof(g_server_ip)-1] = '\0'; parse_ok=1;
        }else if(strcmp(key,"SERVER_PORT") == 0){
            g_server_port = atoi(val); parse_ok=1;
        }
    }
    fclose(fp);
    if(!parse_ok)
    {
        printf("[致命错误]配置文件解析失败！程序退出\n");
        return -1;
    }
    printf("[配置]加载collector.conf成功\n");
    // 中括号包裹IP，方便肉眼观察是否携带空格、隐藏字符
    printf("[配置] GPIO_RUN_STATUS=%d,GPIO_CNT_PRODUCT=%d,BUZZER_GPIO=%d\n",
            g_gpio_run_status,g_gpio_cnt_product,g_gpio_buzzer);
    printf("[配置] TEMP_THRESHOLD=%d, SERVER_IP=[%s] PORT=%d DEV_ID=%d\n",
            g_temp_threshold,g_server_ip,g_server_port,g_device_id);
    return 0;
}

/**
 * @brief sig_handler 信号处理函数，捕获Ctrl+C(SIGINT)实现可控退出
 */
void sig_handler(int sig)
{
    if(sig == SIGINT)
    {
        printf("\n[系统]收到Ctrl+C退出信号，准备安全关闭程序...\n");
        g_run_flag = 0;
        pthread_cond_broadcast(&g_buf_cond);
    }
}

/**
 * @brief ring_buf_push 环形缓冲区写入数据
 * @param pdata 待写入采集数据指针
 * @return 0成功
 */
int ring_buf_push(CollectData_t *pdata)
{
    pthread_mutex_lock(&g_buf_mutex);
    int next_wr = (g_ring_wr + 1) % g_ring_buf_size;
    // 缓冲区已满，丢弃最旧一条数据
    if(next_wr == g_ring_rd)
    {
        g_ring_rd = (g_ring_rd +1) % g_ring_buf_size;
    }
    g_ring_buf[g_ring_wr] = *pdata;
    g_ring_wr = next_wr;
    pthread_cond_signal(&g_buf_cond); // 唤醒阻塞等待的消费者线程
    pthread_mutex_unlock(&g_buf_mutex);
    return 0;
}

/**
 * @brief ring_buf_pop 环形缓冲区读取数据，无数据会阻塞休眠
 * @param pout 输出采集数据
 * @return 0读取成功；-1收到退出信号
 */
int ring_buf_pop(CollectData_t *pout)
{
    pthread_mutex_lock(&g_buf_mutex);
    // 缓冲区为空并且程序运行中，阻塞等待新数据
    while(g_ring_wr == g_ring_rd && g_run_flag)
    {
        pthread_cond_wait(&g_buf_cond, &g_buf_mutex);
    }
    if(!g_run_flag)
    {
        pthread_mutex_unlock(&g_buf_mutex);
        return -1;
    }
    *pout = g_ring_buf[g_ring_rd];
    g_ring_rd = (g_ring_rd +1) % g_ring_buf_size;
    pthread_mutex_unlock(&g_buf_mutex);
    return 0;
}

/**
 * @brief producer_thread 生产者线程【成员B：硬件采集业务】
 * 只做真实硬件采集：E18-D80NK、DHT11、GPIO设备状态、蜂鸣器本地报警
 * E18-D80NK逻辑：检测物体遮挡 → product_cnt=1；无物体 → product_cnt=0，瞬时状态，不做累加
 */
void *producer_thread(void *arg)
{
    (void)arg;
    // 保存上一次DHT11有效值，读取失败时复用，静态变量只初始化一次
    static uint8_t last_temp = 25;
    static uint8_t last_humi = 40;
    while(g_run_flag)
    {
        CollectData_t data;
        memset(&data,0,sizeof(data));
        data.timestamp = time(NULL); // 获取当前时间戳
        
        //====================真实硬件采集====================
        //1.读取设备运行状态，杜邦线短接GND=运行
        int run_val = gpio_read_debounce(g_gpio_run_status,20);
        printf("[DEBUG] run_val=%d\n", run_val);
        if(run_val == 0)
        {
            data.dev_run = 1;
        }
        else
        {
            data.dev_run = 0;
        }
        //2.E18-D80NK红外传感器：瞬时状态，检测物体=1，无物体=0
        int cnt_val = gpio_read_debounce(g_gpio_cnt_product,20);
        if(cnt_val != -1)
        {
            if(cnt_val == 0)
            {
                data.product_cnt = 1;
            }
            else
            {
                data.product_cnt = 0;
            }
        }
        else
        {
            // 读取硬件失败，默认赋值0（无物体）
            data.product_cnt = 0;
        }
        printf("[红外状态] 当前产量标识：%u\n", data.product_cnt);
        //3.DHT11温湿度，调用内核/dev/dht11驱动
        uint8_t temp_buf = 0;
        uint8_t humi_buf = 0;
        int dht_ret = dht11_read(0, &temp_buf, &humi_buf);
        if(dht_ret == 0)
        {
            // 读取成功，更新历史缓存
            data.temp = temp_buf;
            data.humi = humi_buf;
            last_temp = temp_buf;
            last_humi = humi_buf;
            printf("[DHT11]温度:%d℃ 湿度:%d%%RH 设备状态:%d\n",
                    data.temp, data.humi, data.dev_run);
        }
        else
        {
            // 读取失败，直接使用历史有效值，忽略buf里面的0值
            printf("[DHT11警告]读取失败，保留上一次有效值 temp=%d humi=%d\n",last_temp,last_humi);
            data.temp = last_temp;
            data.humi = last_humi;
        }
        //4.温度超过阈值蜂鸣器本地报警
        time_t now = time(NULL);
        if(data.temp >= g_temp_threshold)
        {
            if((now - g_last_alarm_tm) > g_alarm_cooldown)
            {
                printf("[报警]温度超限！当前温度：%d℃\n",data.temp);
                buzzer_alarm(g_gpio_buzzer);
                g_last_alarm_tm = now;
            }
        }
        // 将采集数据压入环形缓冲区，交给TCP消费者线程
        ring_buf_push(&data);
        sleep(1); // DHT11最小采样周期1秒，不可更小
    }
    printf("生产者【硬件采集】线程退出\n");
    return NULL;
}

/**
 * @brief consumer_thread 消费者线程【TCP网络上传业务】
 * 功能：
 *   1. 断线自动重连
 *   2. 读取环形缓冲区采集数据并上报 (type=0x01)
 *   3. 周期发送心跳包维持连接 (type=0x02)
 *   【修改点】4. 接收并解析服务端下发的控制指令 (type=0x03)
 * 
 * 【修改说明】原版本只做数据上报和心跳，现增加接收控制指令功能
 */
void *consumer_thread(void *arg)
{
    (void)arg;
    int sockfd = -1;
    struct sockaddr_in serv_addr;
    time_t last_heartbeat = 0;
    uint8_t packet[256];
    int packet_len;
    
    uint8_t recv_buf[4096];
    int recv_len = 0;
    
    fd_set read_fds;
    struct timeval tv;
    
    while (g_run_flag)
    {
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(g_server_port);
        
        int ret_ip = inet_pton(AF_INET, g_server_ip, &serv_addr.sin_addr);
        if(ret_ip <= 0)
        {
            printf("[致命错误！IP解析失败 g_server_ip=[%s] 检查collector.conf!\n", g_server_ip);
            sleep(2);
            sockfd = -1;
            continue;
        }
        
        if (sockfd < 0)
        {
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0)
            {
                perror("socket create error");
                sleep(2);
                continue;
            }
            if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            {
                perror("connect server failed");
                close(sockfd);
                sockfd = -1;
                sleep(2);
                continue;
            }
            printf("[TCP]成功连接服务端 %s:%d (设备ID:%d)\n",
                   g_server_ip, g_server_port, g_device_id);
            last_heartbeat = time(NULL);
            recv_len = 0;
        }

        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int select_ret = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
        
        if (select_ret > 0 && FD_ISSET(sockfd, &read_fds))
        {
            ssize_t n = read(sockfd, recv_buf + recv_len, sizeof(recv_buf) - recv_len - 1);
            if (n > 0)
            {
                recv_len += n;
                
                while (recv_len >= (int)sizeof(ProtocolHeader))
                {
                    ProtocolHeader *hdr = (ProtocolHeader*)recv_buf;
                    
                    // =========================================================
                    // 【关键修复】检查魔数 0x5A5A - 必须使用 ntohs() 转换
                    // =========================================================
                    if (ntohs(hdr->magic) != 0x5A5A)
                    {
                        memmove(recv_buf, recv_buf + 1, recv_len - 1);
                        recv_len--;
                        continue;
                    }
                    
                    uint32_t payload_len = ntohl(hdr->payload_len);
                    int total_len = sizeof(ProtocolHeader) + payload_len;
                    
                    if (recv_len < total_len)
                    {
                        break;
                    }
                    
                    uint16_t stored_crc = ntohs(hdr->crc16);
                    uint8_t crc_buf[8 + 1024];
                    memcpy(crc_buf, recv_buf, 8);
                    memcpy(crc_buf + 8, recv_buf + sizeof(ProtocolHeader), payload_len);
                    uint16_t calc_crc = crc16_calc(crc_buf, 8 + payload_len);
                    
                    if (calc_crc != stored_crc)
                    {
                        printf("[TCP] CRC校验失败，丢弃数据包\n");
                        memmove(recv_buf, recv_buf + total_len, recv_len - total_len);
                        recv_len -= total_len;
                        continue;
                    }
                    
                    // 处理控制指令 (type=0x03)
                    if (hdr->type == 0x03)
                    {
                        if (payload_len >= sizeof(CtrlPayload))
                        {
                            CtrlPayload *ctrl = (CtrlPayload*)(recv_buf + sizeof(ProtocolHeader));
                            uint16_t device_id = ntohs(hdr->device_id);
                            
                            if (device_id == g_device_id || device_id == 0)
                            {
                                printf("[控制] 收到控制指令: 0x%02X (设备ID:%d)\n", 
                                       ctrl->ctrl_code, device_id);
                                
                                switch (ctrl->ctrl_code)
                                {
                                    case 0x01:
                                        printf("[控制] 执行: 打开蜂鸣器 (GPIO%d 输出0)\n", g_gpio_buzzer);
                                        gpio_set_value(g_gpio_buzzer, 0);
                                        break;
                                        
                                    case 0x02:
                                        printf("[控制] 执行: 关闭蜂鸣器 (GPIO%d 输出1)\n", g_gpio_buzzer);
                                        gpio_set_value(g_gpio_buzzer, 1);
                                        break;
                                        
                                    case 0x03:
                                        printf("[控制] 执行: 复位设备\n");
                                        gpio_set_value(g_gpio_buzzer, 1);
                                        break;
                                        
                                    default:
                                        printf("[控制] 未知控制码: 0x%02X\n", ctrl->ctrl_code);
                                        break;
                                }
                            }
                            else
                            {
                                printf("[控制] 忽略设备 %d 的指令 (本机ID:%d)\n", 
                                       device_id, g_device_id);
                            }
                        }
                        else
                        {
                            printf("[控制] 负载长度异常: %u (期望 >= %zu)\n", 
                                   payload_len, sizeof(CtrlPayload));
                        }
                    }
                    else
                    {
                        printf("[TCP] 收到 type=0x%02X 数据包 (长度:%u)\n", 
                               hdr->type, payload_len);
                    }
                    
                    memmove(recv_buf, recv_buf + total_len, recv_len - total_len);
                    recv_len -= total_len;
                }
            }
            else if (n == 0)
            {
                printf("[TCP]服务端关闭连接，准备重连\n");
                close(sockfd);
                sockfd = -1;
                recv_len = 0;
                sleep(1);
                continue;
            }
        }

        // 心跳包发送
        time_t now = time(NULL);
        if (now - last_heartbeat >= 5)
        {
            packet_len = build_heartbeat_packet(packet, sizeof(packet));
            if(packet_len > 0)
            {
                ssize_t wlen = write(sockfd, packet, packet_len);
                if(wlen == packet_len)
                {
                    printf("[HEARTBEAT] 发送心跳包\n");
                    last_heartbeat = now;
                }
                else
                {
                    printf("[TCP]心跳发送失败，断开准备重连\n");
                    close(sockfd);
                    sockfd = -1;
                    recv_len = 0;
                    sleep(1);
                    continue;
                }
            }
        }
        
        // 数据上报
        CollectData_t send_buf;
        int ret = ring_buf_pop(&send_buf);
        if(ret != 0)
        {
            break;
        }
        
        packet_len = pack_collect_data(&send_buf, packet, sizeof(packet));
        if(packet_len < 0)
        {
            printf("[ERROR] 数据打包失败\n");
            continue;
        }
        
        ssize_t wlen = write(sockfd, packet, packet_len);
        if(wlen != packet_len)
        {
            printf("[TCP]发送失败，断开准备重连\n");
            close(sockfd);
            sockfd = -1;
            recv_len = 0;
            sleep(1);
        }
        else
        {
            printf("[DATA] 发送数据: 温度=%d℃ 湿度=%d%%RH 设备状态=%d 产量=%u\n",
                   send_buf.temp, send_buf.humi, send_buf.dev_run, send_buf.product_cnt);
        }
    }
    
    if(sockfd >= 0)
    {
        close(sockfd);
    }
    printf("消费者【TCP上传】线程退出\n");
    return NULL;
}

/**
 * @brief hardware_init GPIO硬件初始化
 * 导出E18、按键、蜂鸣器GPIO
 * 注意：DHT11由内核/dev/dht11驱动接管，不在此处操作GPIO
 */
void hardware_init(void)
{
    // E18-D80NK红外输入
    gpio_export(g_gpio_cnt_product);
    gpio_set_dir(g_gpio_cnt_product,"in");
    // 设备运行状态输入(杜邦线模拟按键)
    gpio_export(g_gpio_run_status);
    gpio_set_dir(g_gpio_run_status,"in");
    // 蜂鸣器输出，上电默认关闭鸣叫
    gpio_export(g_gpio_buzzer);
    gpio_set_dir(g_gpio_buzzer,"out");
    gpio_set_value(g_gpio_buzzer,1);
}

/**
 * @brief hardware_deinit 程序退出释放GPIO资源
 */
void hardware_deinit(void)
{
    gpio_set_value(g_gpio_buzzer,1); // 关闭蜂鸣器
    gpio_unexport(g_gpio_buzzer);
    gpio_unexport(g_gpio_run_status);
    gpio_unexport(g_gpio_cnt_product);
}

int main(void)
{
    pthread_t tid_producer;
    pthread_t tid_consumer;
    
    // 注册Ctrl+C信号处理，实现可控优雅退出
    signal(SIGINT, sig_handler);
    
    // 加载外部配置文件，无配置文件直接退出
    if(load_config_file("./collector.conf") != 0)
    {
        return -1;
    }
    
    // 动态分配环形缓冲区内存
    g_ring_buf = (CollectData_t *)malloc(sizeof(CollectData_t)* g_ring_buf_size);
    if(g_ring_buf == NULL)
    {
        printf("[ERROR]环形缓冲区malloc失败\n");
        return -1;
    }
    g_ring_wr = 0;
    g_ring_rd = 0;
    
    // 初始化互斥锁、条件变量
    pthread_mutex_init(&g_buf_mutex,NULL);
    pthread_cond_init(&g_buf_cond,NULL);
    
    // 初始化GPIO硬件
    hardware_init();
    
    printf("========采集板程序启动========\n");
    printf("提示：杜邦线短接GPIO%d与GND =设备运行；拔掉=停机\n",g_gpio_run_status);
    printf("提示：按下 Ctrl+C 安全退出程序\n");
    printf("[控制] 已启用远程控制功能，等待服务端下发指令\n");
    
    // 创建采集生产者线程、TCP上传消费者线程
    pthread_create(&tid_producer,NULL,producer_thread,NULL);
    pthread_create(&tid_consumer,NULL,consumer_thread,NULL);
    
    // 主线程原地循环等待
    while(g_run_flag)
    {
        sleep(1);
    }
    
    // 等待两个子线程安全结束
    pthread_join(tid_producer,NULL);
    pthread_join(tid_consumer,NULL);
    
    // 销毁锁与条件变量
    pthread_mutex_destroy(&g_buf_mutex);
    pthread_cond_destroy(&g_buf_cond);
    
    // 释放环形缓冲区堆内存
    free(g_ring_buf);
    g_ring_buf = NULL;
    
    // 释放GPIO硬件资源
    hardware_deinit();
    
    printf("程序正常退出，GPIO已释放\n");
    return 0;
}