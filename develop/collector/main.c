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
#include "gpio.h"
#include "dht11.h"

/************************硬件配置宏定义区************************/
//野火EBF6ULL‑S1‑Mini：板子丝印GPIO编号 != Linux sysfs编号
#define GPIO_RUN_STATUS     1      //杜邦线模拟按键，排针丝印GPIO1(sysfs=1)
#define GPIO_CNT_PRODUCT    0      //E18‑D80NK红外传感器，排针丝印GPIO0(sysfs=0)
#define BUZZER_GPIO         3      //蜂鸣器输出，排针丝印GPIO3(sysfs=3)
#define TEMP_THRESHOLD      30      //温度报警阈值，≥30℃蜂鸣器报警
#define ALARM_COOLDOWN      10      //报警冷却时间(秒)，防止连续鸣叫
#define RING_BUF_SIZE       16      //环形缓冲区最大缓存数据条数
#define SERVER_IP           "192.168.14.50"  //成员A服务端电脑局域网IP
#define SERVER_PORT         8888             //TCP服务端端口
/****************************************************************/

static int g_run_flag = 1;              //全局程序退出标志，0代表退出



/**
 * @brief CollectData_t 原始采集结构体
 * 保存采集得到的原始数据，之后打包成自定义协议发送
 * product_cnt：瞬时红外状态 1=检测到物体，0=无物体（瞬时产量标识）
 */
typedef struct
{
    time_t timestamp;       //Unix时间戳，记录采集时刻
    uint8_t temp;           //DHT11温度，单位℃
    uint8_t humi;           //DHT11湿度，单位%RH
    uint8_t dev_run;        //设备状态：0停机，1运行
    uint32_t product_cnt;   //红外瞬时状态：1检测物体，0无物体
}CollectData_t;

/************************ 自定义通信协议（和成员A服务器约定） ************************/
#pragma pack(push, 1)
/**
 * @brief ProtocolHeader 协议包头 12字节
 */
typedef struct {
    uint16_t magic;       //魔数 固定0x5A5A，用于校验数据包开头
    uint32_t payload_len; //负载数据长度
    uint8_t type;         //报文类型 0x01数据上报  0x02心跳包
    uint8_t version;      //协议版本号，固定1
    uint16_t device_id;   //采集板设备编号
    uint16_t crc16;       //Modbus CRC16校验码
} ProtocolHeader;

/**
 * @brief DataPayload 数据上报负载，type=0x01时有效，14字节
 * production字段现在传输红外瞬时状态：1有物体，0无物体
 */
typedef struct {
    uint32_t timestamp;      //Unix时间戳（网络字节序）
    int16_t  temperature;    //温度×100，例25℃ → 2500
    uint8_t  status;         //设备运行状态 0停机 1运行
    uint8_t  humi;           //环境湿度
    int32_t  production;     //红外瞬时状态：1检测物体 / 0无物体
    uint8_t  reserved[3];    //保留字节，填0
} DataPayload;
#pragma pack(pop)

#define DEVICE_ID 1001  //本采集板设备ID

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
 * @return 成功返回完整报文字节长度；失败返回‑1
 */
static int pack_collect_data(CollectData_t *src, uint8_t *out_buf, int buf_size) {
    if (buf_size < (int)(sizeof(ProtocolHeader) + sizeof(DataPayload))) {
        return -1;
    }
    ProtocolHeader *hdr = (ProtocolHeader*)out_buf;
    DataPayload *payload = (DataPayload*)(out_buf + sizeof(ProtocolHeader));

    //填充负载，全部转换网络大端字节序
    payload->timestamp = htonl((uint32_t)src->timestamp);
    payload->temperature = htons((int16_t)(src->temp * 100));
    payload->status = src->dev_run;
    payload->humi = src->humi;
    payload->production = htonl((int32_t)src->product_cnt);
    memset(payload->reserved, 0, 3);

    //填充协议包头
    hdr->magic = htons(0x5A5A);
    hdr->payload_len = htonl(sizeof(DataPayload));
    hdr->type = 0x01;
    hdr->version = 1;
    hdr->device_id = htons(DEVICE_ID);
    hdr->crc16 = 0; //计算校验之前先置0

    //CRC计算范围：包头前8字节 + 完整负载
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
 * @return 报文长度，失败‑1
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
    hdr->device_id = htons(DEVICE_ID);
    hdr->crc16 = htons(crc16_calc((uint8_t*)out_buf, 8));
    return sizeof(ProtocolHeader);
}

/************************ 协议定义结束 ************************/

//环形缓冲区：生产者(采集线程)写，消费者(TCP上传线程)读
CollectData_t g_ring_buf[RING_BUF_SIZE];
int g_ring_wr = 0;      //写索引
int g_ring_rd = 0;      //读索引
pthread_mutex_t g_buf_mutex;    //互斥锁，保护多线程访问缓冲区
pthread_cond_t g_buf_cond;      //条件变量：无数据时消费者线程休眠

static time_t g_last_alarm_tm = 0;      //记录上一次蜂鸣器报警时间

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
    int next_wr = (g_ring_wr + 1) % RING_BUF_SIZE;
    //缓冲区已满，丢弃最旧一条数据
    if(next_wr == g_ring_rd)
    {
        g_ring_rd = (g_ring_rd +1) % RING_BUF_SIZE;
    }
    g_ring_buf[g_ring_wr] = *pdata;
    g_ring_wr = next_wr;
    pthread_cond_signal(&g_buf_cond); //唤醒阻塞等待的消费者线程
    pthread_mutex_unlock(&g_buf_mutex);
    return 0;
}

/**
 * @brief ring_buf_pop 环形缓冲区读取数据，无数据会阻塞休眠
 * @param pout 输出采集数据
 * @return 0读取成功；‑1收到退出信号
 */
int ring_buf_pop(CollectData_t *pout)
{
    pthread_mutex_lock(&g_buf_mutex);
    //缓冲区为空并且程序运行中，阻塞等待新数据
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
    g_ring_rd = (g_ring_rd +1) % RING_BUF_SIZE;
    pthread_mutex_unlock(&g_buf_mutex);
    return 0;
}

/**
 * @brief producer_thread 生产者线程【成员B：硬件采集业务】
 * 只做真实硬件采集：E18‑D80NK、DHT11、GPIO设备状态、蜂鸣器报警
 * E18‑D80NK逻辑：检测物体遮挡 → product_cnt=1；无物体 → product_cnt=0，瞬时状态，不做累加
 */
void *producer_thread(void *arg)
{
    (void)arg;
    //保存上一次DHT11有效值，读取失败时复用，避免上报0℃
    static uint8_t last_temp = 25;
    static uint8_t last_humi = 40;

    while(g_run_flag)
    {
        CollectData_t data;
        memset(&data,0,sizeof(data));
        data.timestamp = time(NULL); //获取当前时间戳

        //====================真实硬件采集====================
        //1.读取设备运行状态 GPIO_RUN_STATUS(1)，杜邦线短接GND=运行
        int run_val = gpio_read_debounce(GPIO_RUN_STATUS,20);
        printf("[DEBUG] run_val=%d\n", run_val);
        if(run_val == 0)
        {
            data.dev_run = 1;
        }
        else
        {
            data.dev_run = 0;
        }

        //2.E18‑D80NK红外传感器：瞬时状态，检测物体=1，无物体=0
        int cnt_val = gpio_read_debounce(GPIO_CNT_PRODUCT,20);
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
            //读取硬件失败，默认赋值0（无物体）
            data.product_cnt = 0;
        }
        printf("[红外状态] 当前产量标识：%u\n", data.product_cnt);

        //3.DHT11温湿度，调用内核/dev/dht11驱动，第一个参数传0占位
        uint8_t temp_buf = 0;
        uint8_t humi_buf = 0;
        if(dht11_read(0, &temp_buf, &humi_buf) == 0)
        {
            data.temp = temp_buf;
            data.humi = humi_buf;
            last_temp = temp_buf;
            last_humi = humi_buf;
            printf("[DHT11]温度:%d℃ 湿度:%d%%RH 设备状态:%d\n",
                    data.temp, data.humi, data.dev_run);
        }
        else
        {
            printf("[DHT11警告]读取失败，保留上一次有效值\n");
            data.temp = last_temp;
            data.humi = last_humi;
        }

        //4.温度超过阈值蜂鸣器报警 BUZZER_GPIO
        time_t now = time(NULL);
        if(data.temp >= TEMP_THRESHOLD)
        {
            if((now - g_last_alarm_tm) > ALARM_COOLDOWN)
            {
                printf("[报警]温度超限！当前温度：%d℃\n",data.temp);
                buzzer_alarm(BUZZER_GPIO);
                g_last_alarm_tm = now;
            }
        }

        //将采集数据压入环形缓冲区，交给TCP消费者线程
        ring_buf_push(&data);
        sleep(1); //DHT11最小采样周期1秒，不可更小
    }
    printf("生产者【硬件采集】线程退出\n");
    return NULL;
}

/**
 * @brief consumer_thread 消费者线程【成员A：TCP网络上传业务】
 * 功能：断线自动重连、读取环形缓冲区、协议打包、周期发送心跳包
 */
void *consumer_thread(void *arg)
{
    (void)arg;
    int sockfd = -1;
    struct sockaddr_in serv_addr;
    time_t last_heartbeat = 0;
    uint8_t packet[256];
    int packet_len;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    while (g_run_flag)
    {
        //socket无效，执行连接服务端
        if (sockfd < 0)
        {
            sockfd = socket(AF_INET,SOCK_STREAM,0);
            if (sockfd < 0)
            {
                perror("socket create error");
                sleep(2);
                continue;
            }
            if(connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
            {
                perror("connect server failed");
                close(sockfd);
                sockfd = -1;
                sleep(2);
                continue;
            }
            printf("[TCP]成功连接服务端 %s:%d (设备ID:%d)\n",
                   SERVER_IP, SERVER_PORT, DEVICE_ID);
            last_heartbeat = time(NULL);
        }

        //每5秒发送一次心跳包，维持长连接
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
                    sleep(1);
                    continue;
                }
            }
        }

        //从环形缓冲区取出采集数据
        CollectData_t send_buf;
        int ret = ring_buf_pop(&send_buf);
        if(ret != 0)
        {
            break;
        }

        //按照自定义协议打包报文
        packet_len = pack_collect_data(&send_buf, packet, sizeof(packet));
        if(packet_len < 0)
        {
            printf("[ERROR] 数据打包失败\n");
            continue;
        }

        //发送二进制协议包
        ssize_t wlen = write(sockfd, packet, packet_len);
        if(wlen != packet_len)
        {
            printf("[TCP]发送失败，断开准备重连\n");
            close(sockfd);
            sockfd = -1;
            sleep(1);
        }
        else
        {
            //打印完整上报信息：温度、湿度、设备状态、产量
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
    //E18‑D80NK红外输入
    gpio_export(GPIO_CNT_PRODUCT);
    gpio_set_dir(GPIO_CNT_PRODUCT,"in");
    //设备运行状态输入(杜邦线模拟按键)
    gpio_export(GPIO_RUN_STATUS);
    gpio_set_dir(GPIO_RUN_STATUS,"in");
    //蜂鸣器输出，上电默认关闭鸣叫
    gpio_export(BUZZER_GPIO);
    gpio_set_dir(BUZZER_GPIO,"out");
    gpio_set_value(BUZZER_GPIO,1);
}

/**
 * @brief hardware_deinit 程序退出释放GPIO资源
 */
void hardware_deinit(void)
{
    gpio_set_value(BUZZER_GPIO,1); //关闭蜂鸣器
    gpio_unexport(BUZZER_GPIO);
    gpio_unexport(GPIO_RUN_STATUS);
    gpio_unexport(GPIO_CNT_PRODUCT);
}

int main(void)
{
    pthread_t tid_producer;
    pthread_t tid_consumer;

    //注册Ctrl+C信号处理，实现可控优雅退出
    signal(SIGINT, sig_handler);

    //初始化互斥锁、条件变量
    pthread_mutex_init(&g_buf_mutex,NULL);
    pthread_cond_init(&g_buf_cond,NULL);

    //初始化GPIO硬件
    hardware_init();
    printf("========采集板程序启动========\n");
    printf("提示：杜邦线短接丝印GPIO1与GND =设备运行；拔掉=停机\n");
    printf("提示：按下 Ctrl+C 安全退出程序\n");

    //创建采集生产者线程、TCP上传消费者线程
    pthread_create(&tid_producer,NULL,producer_thread,NULL);
    pthread_create(&tid_consumer,NULL,consumer_thread,NULL);

    //主线程原地循环等待
    while(g_run_flag)
    {
        sleep(1);
    }

    //等待两个子线程安全结束
    pthread_join(tid_producer,NULL);
    pthread_join(tid_consumer,NULL);

    //销毁锁与条件变量
    pthread_mutex_destroy(&g_buf_mutex);
    pthread_cond_destroy(&g_buf_cond);

    //释放GPIO硬件资源
    hardware_deinit();
    printf("程序正常退出，GPIO已释放\n");
    return 0;
}
