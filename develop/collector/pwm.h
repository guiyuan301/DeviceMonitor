#ifndef __PWM_H__
#define __PWM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 舵机PWM控制（sysfs接口，50Hz/20ms周期）
 *
 * 典型硬件：SG90-360 连续旋转舵机
 *   周期 20ms（50Hz）
 *   高电平脉宽 500000ns(0.5ms)  = 全速逆时针
 *               1000000ns(1.0ms)= 慢速逆时针
 *               1500000ns(1.5ms)= 停止转动（中点）
 *               2000000ns(2.0ms)= 慢速顺时针
 *               2500000ns(2.5ms)= 全速顺时针
 *   注意：360度舵机脉宽控制的是“转速/方向”，不是角度！
 *
 * 组号约定：与原命令行工具一致，组号从1开始计数，
 *           传入 3 对应 /sys/class/pwm/pwmchip2/pwm0
 */

/**
 * @brief 初始化舵机PWM：导出pwm0通道、设置20ms周期、默认失能
 * @param chip_group PWM组号（从1开始，传3对应pwmchip2）
 * @return 成功返回0，失败返回-1
 */
int servo_init(int chip_group);

/**
 * @brief 启动舵机旋转：写入转速脉宽并使能PWM输出
 * @param duty_ns 高电平脉宽，单位纳秒（500000~2500000，非1500000中点才会转）
 * @return 成功返回0，失败返回-1
 */
int servo_start(uint32_t duty_ns);

/**
 * @brief 停止舵机旋转：输出1.5ms中点脉宽主动停转（保持使能与信号输出）
 * @param stop_duty_ns 停转中点脉宽，单位纳秒（360度舵机固定1500000）
 * @return 成功返回0，失败返回-1
 */
int servo_stop(uint32_t stop_duty_ns);

/**
 * @brief 释放舵机资源：失能PWM并取消导出pwm0通道
 */
void servo_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
