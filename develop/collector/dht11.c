#include "dht11.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

//静态文件描述符，只打开一次/dev/dht11
static int dht_fd = -1;

/**
 * @brief DHT11读取温湿度（调用内核驱动/dev/dht11，gpio_num参数占位，实际引脚由设备树配置）
 * @param gpio_num 占位参数，兼容旧接口，不起实际作用
 * @param temp  返回温度(℃)
 * @param humi  返回湿度(%RH)
 * @return 0读取成功，-1校验失败/超时
 */
int dht11_read(int gpio_num, uint8_t *temp, uint8_t *humi)
{
    unsigned char buf[6] = {0};
    int ret_len;

    //第一次调用打开设备节点
    if(dht_fd < 0)
    {
        dht_fd = open("/dev/dht11", O_RDONLY);
        if(dht_fd < 0)
        {
            perror("open /dev/dht11 failed");
            return -1;
        }
    }

    ret_len = read(dht_fd, buf, sizeof(buf));
    if(ret_len <= 0)
    {
        perror("read /dev/dht11");
        return -1;
    }

    // buf[0]湿度整数，buf[2]温度整数，和野火内核驱动输出格式匹配
    *humi = buf[0];
    *temp = buf[2];

    return 0;
}
