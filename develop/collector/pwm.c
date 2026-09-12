#include "pwm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define EMSG(errmsg) {fprintf(stderr,"%d:",__LINE__);perror(errmsg);}

#define PERIOD 20000000  // 20ms周期（50Hz舵机标准信号）

static char pwm_path[128];  // /sys/class/pwm/pwmchipN/pwm0
static int  s_chip_num = -1; // pwmchip编号（组号-1）
static int  s_inited = 0;

//往路径为attr的这个文件里，写入数据val
static int write_pwm(const char *attr, const char *val)
{
    char file_path[128];
    int len;
    int fd;
    snprintf(file_path, sizeof(file_path), "%s/%s", pwm_path, attr);
    if (0 > (fd = open(file_path, O_WRONLY))) {
        EMSG("open error");
        return fd;
    }
    printf("file_path:%s\n", file_path);
    len = strlen(val);
    if (len != write(fd, val, len)) {
        EMSG("write error");
        close(fd);
        return -1;
    }
    close(fd);  //关闭文件
    return 0;
}

//初始化舵机PWM（参数：组号，从1开始，传3对应pwmchip2）
int servo_init(int chip_group)
{
    char temp[128];
    char period_val[32];
    int fd;

    if (chip_group <= 0) {
        fprintf(stderr, "[舵机]PWM组号非法: %d\n", chip_group);
        return -1;
    }

    //将接收的组号改为对应目录的编号
    s_chip_num = chip_group - 1;
    snprintf(pwm_path, sizeof(pwm_path),
             "/sys/class/pwm/pwmchip%d/pwm0", s_chip_num);
    printf("[舵机]path:%s\n", pwm_path);

    //通过access判断通道是否已经被导出
    if (access(pwm_path, F_OK))
    {
        //文件不存在，进行导出
        snprintf(temp, sizeof(temp),
                 "/sys/class/pwm/pwmchip%d/export", s_chip_num);
        fd = open(temp, O_WRONLY);
        if (fd < 0)
        {
            EMSG("export open error");
            return -1;
        }
        //echo 0 > export
        if (write(fd, "0", 1) != 1)
        {
            EMSG("export write error");
            close(fd);
            return -1;
        }
        close(fd);  //关闭文件
        //等待sysfs节点生成
        usleep(100000);
    }

    /* 严格按顺序配置：周期 -> 占空比 -> 使能
       新导出的pwm0通道enable默认为0，可直接写period */
    snprintf(period_val, sizeof(period_val), "%d", PERIOD);
    if (write_pwm("period", period_val) != 0) {
        return -1;
    }
    if (write_pwm("duty_cycle", "0") != 0) {
        return -1;
    }

    s_inited = 1;
    printf("[舵机]PWM初始化成功 pwmchip%d, period=%dns(50Hz)\n",
           s_chip_num, PERIOD);
    return 0;
}

//启动舵机（参数：高电平脉宽，单位纳秒）
int servo_start(uint32_t duty_ns)
{
    char period_val[32];
    char duty_val[32];

    if (!s_inited) {
        fprintf(stderr, "[舵机]PWM未初始化，无法启动\n");
        return -1;
    }

    //先确保这个占空比不会大于周期
    if (duty_ns > PERIOD) {
        duty_ns = PERIOD;
    }

    /* 严格按顺序配置：周期 -> 转速占空比 -> 使能
       每次启动都重配三要素，保证从停转中点切到旋转脉宽时序确定 */
    snprintf(period_val, sizeof(period_val), "%d", PERIOD);
    if (write_pwm("period", period_val) != 0) {
        return -1;
    }
    snprintf(duty_val, sizeof(duty_val), "%u", duty_ns);
    if (write_pwm("duty_cycle", duty_val) != 0) {
        return -1;
    }
    /* 最后使能pwm */
    if (write_pwm("enable", "1") != 0) {
        return -1;
    }
    printf("[舵机]旋转 period=%dns duty_cycle=%uns(转速脉宽) enable=1\n",
           PERIOD, duty_ns);
    return 0;
}

//停止舵机旋转：输出1.5ms中点脉宽主动停转（360度连续旋转舵机规范）
//保持使能和信号输出，舵机收到中点信号会主动刹停；彻底断电由servo_deinit完成
int servo_stop(uint32_t stop_duty_ns)
{
    char period_val[32];
    char duty_val[32];

    if (!s_inited) {
        return -1;
    }

    //严格按顺序：周期 -> 停转中点占空比 -> 使能
    snprintf(period_val, sizeof(period_val), "%d", PERIOD);
    if (write_pwm("period", period_val) != 0) {
        return -1;
    }
    snprintf(duty_val, sizeof(duty_val), "%u", stop_duty_ns);
    if (write_pwm("duty_cycle", duty_val) != 0) {
        return -1;
    }
    if (write_pwm("enable", "1") != 0) {
        return -1;
    }
    printf("[舵机]停止 duty_cycle=%uns(1.5ms中点停转) enable=1\n",
           stop_duty_ns);
    return 0;
}

//释放舵机：失能并取消导出pwm0通道
void servo_deinit(void)
{
    char temp[128];
    int fd;

    if (!s_inited) {
        return;
    }

    //第一步：先关闭PWM输出
    write_pwm("enable", "0");

    //第二步：再释放通道 unexport
    snprintf(temp, sizeof(temp),
             "/sys/class/pwm/pwmchip%d/unexport", s_chip_num);
    fd = open(temp, O_WRONLY);
    if (fd < 0)
    {
        EMSG("free_open error");
        return;
    }
    //echo 0 > unexport
    if (write(fd, "0", 1) != 1)
    {
        EMSG("free_write error");
    }
    close(fd);  //关闭文件
    s_inited = 0;
    printf("[舵机]PWM通道已释放\n");
}
