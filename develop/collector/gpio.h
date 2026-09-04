#ifndef __GPIO_H__
#define __GPIO_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief 导出GPIO引脚，使能sysfs操作
 * @param gpio_num GPIO芯片编号
 * @return 0成功，-1失败
 */
int gpio_export(int gpio_num);

/**
 * @brief 设置GPIO方向："in"输入 / "out"输出
 * @param gpio_num GPIO芯片编号
 * @param dir "in"或者"out"
 * @return 0成功，-1失败
 */
int gpio_set_dir(int gpio_num, const char *dir);

/**
 * @brief 设置输出引脚电平
 * @param gpio_num GPIO芯片编号
 * @param val 1高电平，0低电平
 * @return 0成功，-1失败
 */
int gpio_set_value(int gpio_num, int val);

/**
 * @brief 读取GPIO输入引脚电平
 * @param gpio_num GPIO芯片编号
 * @return 返回0/1，-1读取出错
 */
int gpio_read_value(int gpio_num);

/**
 * @brief 取消导出GPIO，释放sysfs资源
 * @param gpio_num GPIO芯片编号
 * @return 0成功，-1失败
 */
int gpio_unexport(int gpio_num);

/**
 * @brief 带软件消抖读取GPIO输入
 * @param gpio_num GPIO芯片编号
 * @param ms 消抖延时，单位毫秒
 * @return 返回稳定电平0/1，-1抖动不稳定
 */
int gpio_read_debounce(int gpio_num, int ms);

/**
 * @brief 蜂鸣器报警：滴滴5声
 * @param gpio_num 蜂鸣器输出GPIO编号
 * @note 电平规则：0鸣叫，1关闭
 */
void buzzer_alarm(int gpio_num);

#endif
