#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <string.h>
#include <time.h>

//强制1字节对齐，保证结构体在内存中紧凑无填充字节
#pragma pack(push,1) //对齐指令

/*===========固定包头：12字节===========*/

//通信协议的数据包头部结构体，用于在设备之间通过网络传输数据
typedef struct
{
	uint16_t magic;       //第[0-1]字节， 识别数据包的开始（魔数）：固定 0x5A5A
	uint32_t payload_len; //第[2-5]字节， 负载长度（单位：字节，不含包头）
	uint8_t type;         //第[6]字节     报文类型: 0x01=数据上报，0x02=心跳
	uint8_t version;      //第[7]字节     协议版本：固定1
	uint16_t device_id;   //第[8-9]字节   设备号：1~65535
	uint16_t crc16;       //第[10-11]字节 CRC16校验:对“包头前8字节+负载”计算
}ProtocolHeader;

/*===========数据上报负载：14字节（type=0x01)===========*/
typedef struct {
    uint32_t timestamp;      // Unix 时间戳（秒）
    int16_t  temperature;    // 温度×100（例：25.36℃ → 2536）
    uint8_t  status;         // 0=停机, 1=运行
	uint8_t  humi;           // 湿度
    int32_t  production;     // 产量计数（累计值）
    uint8_t  reserved[3];    // 保留字段，填 0
} DataPayload;

/* ========== 心跳包负载：空 (type=0x02) ========== */
// 不需要额外结构体

#pragma pack(pop)  //恢复之前的字节对齐设置


/* ========== CRC16 计算函数声明 ========== */
uint16_t crc16_calc(const uint8_t *data, size_t len);

/* ========== 解析结果枚举 ========== */
typedef enum {
    PARSE_OK = 0,           // 成功解析一个数据包
    PARSE_HEARTBEAT = 1,    // 收到心跳包（不产生数据）
    PARSE_INCOMPLETE = -1,  // 数据不完整，需要继续接收
    PARSE_CRC_ERROR = -2,   // CRC 校验失败
    PARSE_MAGIC_ERROR = -3, // 魔数错误（可能丢包错位）
    PARSE_UNKNOWN_TYPE = -4 // 未知报文类型
} ParseResult;

/* ========== 数据包解析函数 ========== */
// 解析缓冲区中的一个数据包，消耗已解析字节（缓冲区前移）
// buf: 接收缓冲区；len: 入参为有效长度，出参为消耗后剩余长度
// out_device_id: 解析出的设备号（仅 PARSE_OK/PARSE_HEARTBEAT 有效，可传 NULL）
// out_payload: 解析出的负载（仅 PARSE_OK 有效）
ParseResult parse_packet(char *buf, int *len, uint16_t *out_device_id, DataPayload *out_payload);

#endif