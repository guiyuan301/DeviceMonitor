#ifndef __DHT11_H__
#define __DHT11_H__
#include <stdint.h>
/**
 * @brief DHT11读取温湿度，底层使用内核/dev/dht11驱动
 * @param unused 兼容旧接口，本参数不再使用，可随便填0
 * @param temp  返回温度(℃)
 * @param humi  返回湿度(%RH)
 * @return 0读取成功，-1校验失败/超时
 */
int dht11_read(int unused, uint8_t *temp, uint8_t *humi);
#endif
