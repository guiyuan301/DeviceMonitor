#include "../include/proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>  // ← 添加这个头文件

/* 声明外部函数（实际在 protocol_parser.c 里） */
extern uint16_t crc16_calc(const uint8_t *data, size_t len);

/* 测试用连接缓冲容器（仅承载 recv_buf/recv_len，解析器已不感知连接结构体） */
#define RECV_BUF_SIZE 4096
typedef struct {
    int fd;
    uint16_t device_id;
    char recv_buf[RECV_BUF_SIZE];
    int recv_len;
    time_t last_heartbeat;
    int online;
} Connection;

/* parse_packet 声明已在 proto.h 中提供 */

/* ========== 辅助：构建一个测试数据包 ========== */
void build_test_packet(uint8_t *buf, int *out_len, 
                       uint16_t device_id, float temp, uint8_t status, int32_t prod) {
    ProtocolHeader *hdr = (ProtocolHeader*)buf;
    
    // 清零缓冲区
    memset(buf, 0, 256);
    
    // ========== 关键修复：使用 htons/htonl 转换为网络字节序 ==========
    hdr->magic = htons(0x5A5A);              // ← 修复1
    hdr->payload_len = htonl(sizeof(DataPayload));  // ← 修复2
    hdr->type = 0x01;
    hdr->version = 1;
    hdr->device_id = htons(device_id);       // ← 修复3
    
    DataPayload *payload = (DataPayload*)(buf + sizeof(ProtocolHeader));
    payload->timestamp = htonl(time(NULL));  // ← 修复4
    payload->temperature = htons((int16_t)(temp * 100));  // ← 修复5
    payload->status = status;
    payload->production = htonl(prod);       // ← 修复6
    memset(payload->reserved, 0, 4);
    
    // 计算 CRC：对"包头前8字节 + 负载"计算，跳过 device_id[8-9] 和 crc16[10-11]
    // 注意：这里计算的是已经转成大端的字节流
    uint8_t crc_buf[8 + sizeof(DataPayload)];
    memcpy(crc_buf, buf, 8);                                            // 包头前8字节
    memcpy(crc_buf + 8, buf + sizeof(ProtocolHeader), sizeof(DataPayload)); // 负载
    hdr->crc16 = htons(crc16_calc(crc_buf, 8 + sizeof(DataPayload)));
    
    *out_len = sizeof(ProtocolHeader) + sizeof(DataPayload);
}

/* ========== 构建心跳包 ========== */
void build_heartbeat_packet(uint8_t *buf, int *out_len, uint16_t device_id) {
    ProtocolHeader *hdr = (ProtocolHeader*)buf;
    
    memset(buf, 0, 256);
    
    // ========== 关键修复：使用 htons 转换为网络字节序 ==========
    hdr->magic = htons(0x5A5A);              // ← 修复7
    hdr->payload_len = 0;                    // 0 不需要转换
    hdr->type = 0x02;
    hdr->version = 1;
    hdr->device_id = htons(device_id);       // ← 修复8
    hdr->crc16 = htons(crc16_calc((uint8_t*)buf, 8));  // CRC转网络字节序存储
    
    *out_len = sizeof(ProtocolHeader);
}

/* ========== 主测试函数 ========== */
int main() {
    printf("========== 协议解析测试开始 ==========\n\n");
    
    int test_passed = 0;
    int test_failed = 0;
    
    // ===== 测试1: 正常数据包解析 =====
    printf("[测试1] 正常数据包解析\n");
    Connection conn;
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t packet[256];
    int packet_len;
    build_test_packet(packet, &packet_len, 1001, 25.36, 1, 12345);
    
    printf("  包长度: %d 字节\n", packet_len);
    printf("  Header大小: %lu 字节\n", sizeof(ProtocolHeader));
    printf("  Payload大小: %lu 字节\n", sizeof(DataPayload));
    
    memcpy(conn.recv_buf, packet, packet_len);
    conn.recv_len = packet_len;
    
    DataPayload parsed_data;
    uint16_t dev_id = 0;
    ParseResult result = parse_packet(conn.recv_buf, &conn.recv_len, &dev_id, &parsed_data);

    if (result == PARSE_OK) {
        conn.device_id = dev_id;
        printf("  ✅ 解析成功\n");
        printf("     设备号: %d\n", conn.device_id);
        printf("     温度: %.2f ℃\n", parsed_data.temperature / 100.0);
        printf("     状态: %s\n", parsed_data.status ? "运行" : "停机");
        printf("     产量: %d\n", parsed_data.production);
        test_passed++;
    } else if (result == PARSE_CRC_ERROR) {
        printf("  ❌ CRC校验失败\n");
        test_failed++;
    } else {
        printf("  ❌ 解析失败 (返回 %d)\n", result);
        test_failed++;
    }
    
    // ===== 测试2: 心跳包解析 =====
    printf("\n[测试2] 心跳包解析\n");
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t hb_packet[256];
    int hb_len;
    build_heartbeat_packet(hb_packet, &hb_len, 1002);
    
    memcpy(conn.recv_buf, hb_packet, hb_len);
    conn.recv_len = hb_len;
    
    dev_id = 0;
    result = parse_packet(conn.recv_buf, &conn.recv_len, &dev_id, &parsed_data);
    if (result == PARSE_HEARTBEAT) {
        conn.device_id = dev_id;
        printf("  ✅ 心跳包识别成功 (设备 %d)\n", conn.device_id);
        test_passed++;
    } else if (result == PARSE_CRC_ERROR) {
        printf("  ❌ CRC校验失败\n");
        test_failed++;
    } else {
        printf("  ❌ 心跳包识别失败 (返回 %d)\n", result);
        test_failed++;
    }
    
    // ===== 测试3: 粘包处理 =====
    printf("\n[测试3] 粘包处理（两个包一起发）\n");
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t p1[256], p2[256];
    int len1, len2;
    build_test_packet(p1, &len1, 1001, 25.36, 1, 12345);
    build_test_packet(p2, &len2, 1002, 30.50, 0, 67890);
    
    memcpy(conn.recv_buf, p1, len1);
    memcpy(conn.recv_buf + len1, p2, len2);
    conn.recv_len = len1 + len2;
    
    int count = 0;
    while (1) {
        result = parse_packet(conn.recv_buf, &conn.recv_len, NULL, &parsed_data);
        if (result == PARSE_OK || result == PARSE_HEARTBEAT) {
            count++;
        } else if (result == PARSE_INCOMPLETE) {
            break;
        } else if (result < 0) {
            printf("  ⚠️ 解析出错 (返回 %d)，停止解析\n", result);
            break;
        } else {
            continue;
        }
    }
    
    if (count == 2) {
        printf("  ✅ 成功解析 2 个粘在一起的包\n");
        test_passed++;
    } else {
        printf("  ❌ 只解析出 %d 个包（期望 2 个）\n", count);
        test_failed++;
    }
    
    // ===== 测试4: 半包处理 =====
    printf("\n[测试4] 半包处理（数据没收全）\n");
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t p3[256];
    int len3;
    build_test_packet(p3, &len3, 1003, 40.00, 1, 999);
    
    // 只拷贝前 10 个字节（连包头都没收全）
    memcpy(conn.recv_buf, p3, 10);
    conn.recv_len = 10;
    
    result = parse_packet(conn.recv_buf, &conn.recv_len, NULL, &parsed_data);
    if (result == PARSE_INCOMPLETE) {
        printf("  ✅ 正确返回 PARSE_INCOMPLETE\n");
        printf("     缓冲区剩余: %d 字节（等待后续数据）\n", conn.recv_len);
        test_passed++;
    } else {
        printf("  ❌ 应返回 PARSE_INCOMPLETE，实际返回 %d\n", result);
        test_failed++;
    }
    
    // ===== 测试5: CRC 错误检测 =====
    printf("\n[测试5] CRC 错误检测\n");
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t p4[256];
    int len4;
    build_test_packet(p4, &len4, 1004, 50.00, 1, 11111);
    
    // 故意修改负载数据中的一个字节破坏 CRC
    // 修改 temperature 字段的低字节（在 payload 起始位置 + 4字节timestamp + 1字节temperature高字节）
    int payload_offset = sizeof(ProtocolHeader);
    p4[payload_offset + 5] = 0xFF;  // 修改 temperature 的低字节
    
    memcpy(conn.recv_buf, p4, len4);
    conn.recv_len = len4;
    
    result = parse_packet(conn.recv_buf, &conn.recv_len, NULL, &parsed_data);
    if (result == PARSE_CRC_ERROR) {
        printf("  ✅ 成功检测到 CRC 错误\n");
        test_passed++;
    } else {
        printf("  ❌ 未能检测到 CRC 错误 (返回 %d)\n", result);
        test_failed++;
    }
    
    // ===== 测试6: 魔数错误恢复 =====
    printf("\n[测试6] 魔数错误恢复（丢包错位）\n");
    memset(&conn, 0, sizeof(Connection));
    conn.fd = -1;
    
    uint8_t p5[256];
    int len5;
    build_test_packet(p5, &len5, 1005, 60.00, 1, 22222);
    
    // 在包前面加一个垃圾字节，模拟错位
    conn.recv_buf[0] = 0xAA;
    memcpy(conn.recv_buf + 1, p5, len5);
    conn.recv_len = len5 + 1;
    
    result = parse_packet(conn.recv_buf, &conn.recv_len, NULL, &parsed_data);
    if (result == PARSE_MAGIC_ERROR) {
        printf("  ✅ 魔数错误，丢弃 1 字节恢复\n");
        printf("     缓冲区剩余: %d 字节\n", conn.recv_len);
        test_passed++;
    } else {
        printf("  ❌ 魔数错误处理失败 (返回 %d)\n", result);
        test_failed++;
    }
    
    // ===== 测试结果汇总 =====
    printf("\n========== 测试结果汇总 ==========\n");
    printf("  通过: %d 项\n", test_passed);
    printf("  失败: %d 项\n", test_failed);
    
    if (test_failed == 0) {
        printf("  ✅ 所有测试通过！\n");
        return 0;
    } else {
        printf("  ❌ 有 %d 项测试失败，请检查代码\n", test_failed);
        return 1;
    }
}
// test