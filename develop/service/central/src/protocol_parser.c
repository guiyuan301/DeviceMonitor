#include "../include/proto.h"
#include <string.h>
#include <arpa/inet.h>

/* ========== CRC16 (Modbus 标准) ========== */
uint16_t crc16_calc(const uint8_t *data, size_t len) {
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

/* ========== 核心解析函数 ==========
 * 解析器只负责解析缓冲区中的字节流，不感知任何连接结构体。
 * buf/len 为入参缓冲区与有效长度；解析成功后缓冲区前移，len 返回剩余长度。
 * 设备号通过 out_device_id 返回（不在解析器内部维护连接状态）。
 */
ParseResult parse_packet(char *buf, int *len, uint16_t *out_device_id, DataPayload *out_payload) {
    if (*len < (int)sizeof(ProtocolHeader)) {
        return PARSE_INCOMPLETE;
    }

    ProtocolHeader *hdr = (ProtocolHeader*)buf;

    // 【1】检查魔数（0x5A5A 大小端一致，无需转换）
    if (hdr->magic != 0x5A5A) {
        memmove(buf, buf + 1, *len - 1);
        (*len)--;
        return PARSE_MAGIC_ERROR;
    }

    // 【2】检查包总长度（payload_len 从网络字节序转为主机字节序）
    uint32_t payload_len = ntohl(hdr->payload_len);
    int total_len = (int)sizeof(ProtocolHeader) + (int)payload_len;

    if (*len < total_len) {
        return PARSE_INCOMPLETE;
    }

    // 【3】检查负载长度是否合理
    if (payload_len > 1024) {
        memmove(buf, buf + total_len, *len - total_len);
        *len -= total_len;
        return PARSE_UNKNOWN_TYPE;
    }

    // 【4】校验 CRC - 对"包头前8字节 + 负载"计算，跳过 device_id[8-9] 和 crc16[10-11]
    uint16_t stored_crc = ntohs(hdr->crc16);  // 网络字节序转为主机字节序
    uint8_t crc_buf[8 + 1024];
    memcpy(crc_buf, buf, 8);                                  // 包头前8字节
    memcpy(crc_buf + 8, buf + sizeof(ProtocolHeader), payload_len); // 负载
    uint16_t calc_crc = crc16_calc(crc_buf, 8 + payload_len);
    if (calc_crc != stored_crc) {
        memmove(buf, buf + total_len, *len - total_len);
        *len -= total_len;
        return PARSE_CRC_ERROR;
    }

    // 【5】根据报文类型处理
    if (hdr->type == 0x01) {
        if (payload_len != sizeof(DataPayload)) {
            memmove(buf, buf + total_len, *len - total_len);
            *len -= total_len;
            return PARSE_UNKNOWN_TYPE;
        }

        DataPayload *payload = (DataPayload*)(buf + sizeof(ProtocolHeader));
        // 将多字节字段从网络字节序转回主机字节序
        out_payload->timestamp   = ntohl(payload->timestamp);
        out_payload->temperature  = ntohs(payload->temperature);
        out_payload->status       = payload->status;
		out_payload->humi         = payload->humi; //湿度
        out_payload->production   = ntohl(payload->production);
        memcpy(out_payload->reserved, payload->reserved, 3);

        if (out_device_id) *out_device_id = ntohs(hdr->device_id);

        memmove(buf, buf + total_len, *len - total_len);
        *len -= total_len;
        return PARSE_OK;

    } else if (hdr->type == 0x02) {
        if (payload_len != 0) {
            memmove(buf, buf + total_len, *len - total_len);
            *len -= total_len;
            return PARSE_UNKNOWN_TYPE;
        }

        if (out_device_id) *out_device_id = ntohs(hdr->device_id);

        memmove(buf, buf + total_len, *len - total_len);
        *len -= total_len;
        return PARSE_HEARTBEAT;

    } else {
        memmove(buf, buf + total_len, *len - total_len);
        *len -= total_len;
        return PARSE_UNKNOWN_TYPE;
    }
}