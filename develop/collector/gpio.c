#include "gpio.h"
#include <string.h>
#include <sys/stat.h>

//sysfs gpio根路径，i.MX6ULL sysfs标准路径
#define SYS_GPIO "/sys/class/gpio"

/**
 * @brief 向sysfs文件写入字符串，内部工具函数
 * @param path 文件路径
 * @param buf 待写入字符串
 * @return 0成功 -1失败
 */
static int write_sysfs(const char *path, const char *buf)
{
    int fd = open(path, O_WRONLY);
    if(fd < 0)
    {
        perror("open sysfs");
        return -1;
    }
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

/**
 * @brief 导出GPIO，操作export文件
 * @param gpio_num GPIO编号
 * @return 0成功 -1失败
 */
int gpio_export(int gpio_num)
{
    char path[64];
    char num[16];
    //拼接gpio路径，判断引脚是否已经导出
    snprintf(path, sizeof(path), "%s/gpio%d", SYS_GPIO, gpio_num);
    if(access(path, F_OK) == 0)
    {
        //已经存在，不需要重复导出
        return 0;
    }
    //把gpio编号写入export文件完成导出
    snprintf(num, sizeof(num), "%d", gpio_num);
    return write_sysfs("/sys/class/gpio/export", num);
}

/**
 * @brief 取消导出GPIO，操作unexport文件
 * @param gpio_num GPIO编号
 * @return 0成功 -1失败
 */
int gpio_unexport(int gpio_num)
{
    char num[16];
    snprintf(num, sizeof(num), "%d", gpio_num);
    return write_sysfs("/sys/class/gpio/unexport", num);
}

/**
 * @brief 设置gpio方向 in / out
 */
int gpio_set_dir(int gpio_num, const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/direction", SYS_GPIO, gpio_num);
    return write_sysfs(path, dir);
}

/**
 * @brief 设置输出引脚电平1/0
 */
int gpio_set_value(int gpio_num, int val)
{
    char path[64];
    char buf[4];
    snprintf(path, sizeof(path), "%s/gpio%d/value", SYS_GPIO, gpio_num);
    snprintf(buf, sizeof(buf), "%d", val);
    return write_sysfs(path, buf);
}

/**
 * @brief 读取输入引脚value
 * @return 0/1，-1出错
 */
int gpio_read_value(int gpio_num)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/value", SYS_GPIO, gpio_num);
    int fd = open(path, O_RDONLY);
    if(fd < 0)
    {
        perror("gpio read open");
        return -1;
    }
    char ch;
    read(fd, &ch, 1);
    close(fd);
    //把字符'0'/'1'转为数字0、1返回
    return ch - '0';
}

/**
 * @brief GPIO软件消抖
 * @param ms 等待毫秒数
 * @return 稳定电平，-1抖动
 */
int gpio_read_debounce(int gpio_num, int ms)
{
    int v1 = gpio_read_value(gpio_num);
    usleep(ms * 1000);   //延时ms
    int v2 = gpio_read_value(gpio_num);
    if(v1 == v2)
    {
        //前后两次读取一致，电平稳定
        return v1;
    }
    return -1; //抖动，读取无效
}

/**
 * @brief 蜂鸣器滴滴报警5次，每次响1秒停1秒
 * @param gpio_num 蜂鸣器输出引脚
 * @note 有源蜂鸣器：0响，1关闭，结束强制关闭蜂鸣器
 */
void buzzer_alarm(int gpio_num)
{
    for(int i = 0; i < 5; i++)
    {
        gpio_set_value(gpio_num, 1);
        sleep(1);
        gpio_set_value(gpio_num, 0);
        sleep(1);
    }
    gpio_set_value(gpio_num, 1); //确保结束关闭蜂鸣器
}
