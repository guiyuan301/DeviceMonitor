/*
 * ============================================================================
 * alarm.c — 告警引擎实现
 * ============================================================================
 * 内部维护两段动态数组：
 *   - g_rules：从 alarm_rules 加载的规则（reload 时重建）
 *   - g_open ：当前"告警中"的 (device_id, rule_id) → alarm_records.id
 * 设计目标：同一设备同一条规则，告警只产生一条"告警中"记录；
 *           条件不满足时自动恢复（置 status=1）。避免刷屏。
 * ============================================================================
 */
#include "alarm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

 /* ------------------- 内部状态 ------------------- */
 // 内存中正在告警的条目：设备id、规则id、数据库告警记录id
typedef struct {
    int device_id;   /* 告警设备 */
    int rule_id;     /* 命中的规则 */
    int alarm_id;    /* alarm_records 中的记录 id */
} open_alarm_t;

static alarm_rule_t* g_rules = NULL;   /* 内存缓存告警规则数组，从数据库加载 */
static int  g_rule_cnt = 0;            /* 当前有效规则数量 */
static int  g_rule_cap = 0;            /* 规则数组内存容量（realloc扩容用） */

static open_alarm_t* g_open = NULL;    /* 内存缓存：当前处于告警中的集合，防止重复告警刷屏 */
static int  g_open_cnt = 0;            /* 当前告警中的条目数量 */
static int  g_open_cap = 0;            /* 告警中数组内存容量（realloc扩容用） */

/* ------------------- 内部工具 ------------------- */
/**
 * @brief  db_rule_foreach 的回调函数，把数据库读出的告警规则存入内存g_rules数组
 * @param  r     单条告警规则
 * @param  ud    用户透传数据，本代码未使用
 * @return 0成功，非0终止遍历
 */
static int collect_rule(const alarm_rule_t* r, void* ud)
{
    (void)ud;
    // 当前元素数量达到容量，需要扩容
    if (g_rule_cnt >= g_rule_cap) {
        // 没有容量则初始分配16，否则扩大为原来2倍
        int nc = g_rule_cap ? g_rule_cap * 2 : 16;
        alarm_rule_t* nr = (alarm_rule_t*)realloc(g_rules, (size_t)nc * sizeof(alarm_rule_t));
        if (!nr)
            return -1;
        g_rules = nr;
        g_rule_cap = nc;
    }
    // 将规则拷贝到内存数组
    g_rules[g_rule_cnt++] = *r;
    return 0;
}

/**
 * @brief  在告警中数组g_open查找 指定设备+规则是否正在告警
 * @param  device_id 设备编号
 * @param  rule_id   告警规则id
 * @return 找到返回数组下标；找不到返回 -1
 */
static int find_open(int device_id, int rule_id)
{
    for (int i = 0; i < g_open_cnt; i++) {
        if (g_open[i].device_id == device_id && g_open[i].rule_id == rule_id)
            return i;
    }
    return -1;
}

/**
 * @brief 从g_open数组移除指定下标的告警条目，后面元素向前补齐
 * @param idx 待删除元素下标
 */
static void remove_open(int idx)
{
    if (idx < 0 || idx >= g_open_cnt)
        return;
    // 数组元素前移覆盖被删除项
    for (int i = idx; i < g_open_cnt - 1; i++)
        g_open[i] = g_open[i + 1];
    g_open_cnt--;
}

/**
 * @brief 新增一条“告警中”记录到内存g_open数组
 * @param device_id 告警设备id
 * @param rule_id   触发告警的规则id
 * @param alarm_id  数据库alarm_records表生成的告警记录id
 * @return 0成功，-1内存分配失败
 */
static int push_open(int device_id, int rule_id, int alarm_id)
{
    // 容量不足，二倍扩容，初始容量16
    if (g_open_cnt >= g_open_cap) {
        int nc = g_open_cap ? g_open_cap * 2 : 16;
        open_alarm_t* no = (open_alarm_t*)realloc(g_open, (size_t)nc * sizeof(open_alarm_t));
        if (!no)
            return -1;
        g_open = no;
        g_open_cap = nc;
    }
    // 写入新告警中条目
    g_open[g_open_cnt].device_id = device_id;
    g_open[g_open_cnt].rule_id = rule_id;
    g_open[g_open_cnt].alarm_id = alarm_id;
    g_open_cnt++;
    return 0;
}

/**
 * @brief 根据field字段名，从采样结构体取出对应数值
 * @param s     采样数据
 * @param field 字段字符串：temperature/humidity/run_status/output_count
 * @return 返回对应字段double数值；未知字段返回0.0
 */
static double field_value(const sensor_sample_t* s, const char* field)
{
    if (strcmp(field, "temperature") == 0)
        return s->temperature;
    if (strcmp(field, "humidity") == 0)
        return s->humidity;
    if (strcmp(field, "run_status") == 0)
        return (double)s->run_status;
    if (strcmp(field, "output_count") == 0)
        return (double)s->output_count;
    return 0.0;
}

/**
 * @brief 执行比较运算：v OP th，判断是否命中告警条件
 * @param v 采样得到的实际值
 * @param op 运算符字符串 > >= < <= = == !=
 * @param th 告警阈值
 * @return true条件命中；false不命中或运算符非法
 */
static bool compare(double v, const char* op, double th)
{
    if (strcmp(op, ">") == 0)  return v > th;
    if (strcmp(op, ">=") == 0) return v >= th;
    if (strcmp(op, "<") == 0)  return v < th;
    if (strcmp(op, "<=") == 0) return v <= th;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) return v == th;
    if (strcmp(op, "!=") == 0) return v != th;
    return false;   /* 未知比较符视为不命中 */
}

/**
 * @brief 获取当前Unix时间戳（秒）
 * @return int64时间戳
 */
static int64_t now_ts(void)
{
    return (int64_t)time(NULL);
}

/* ------------------- 对外接口 ------------------- */

/**
 * @brief 告警引擎初始化，加载数据库告警规则到内存
 * @return DB_OK成功，其他错误码代表失败
 */
int alarm_init(void)
{
    return alarm_reload_rules();
}

/**
 * @brief 销毁告警引擎，释放内部动态数组内存
 */
void alarm_destroy(void)
{
    free(g_rules); g_rules = NULL; g_rule_cnt = g_rule_cap = 0;
    free(g_open);  g_open = NULL; g_open_cnt = g_open_cap = 0;
}

/**
 * @brief 重新从数据库alarm_rules加载启用的告警规则，刷新内存缓存
 * @note 注意：会清空内存中g_rules、g_open；数据库里告警记录不会删除
 * @return DB_OK成功，其他错误码代表数据库操作失败
 */
int alarm_reload_rules(void)
{
    int rc;
    /* 清空旧规则与旧告警状态（告警记录本身在库中保留） */
    free(g_rules); g_rules = NULL; g_rule_cnt = g_rule_cap = 0;
    free(g_open);  g_open = NULL; g_open_cnt = g_open_cap = 0;
    // filter_device传0，读取全部启用规则，回调collect_rule存入内存
    rc = db_rule_foreach(0, collect_rule, NULL);
    return rc;
}

/**
 * @brief 对一条采样数据执行全部告警规则判断，触发告警/恢复告警
 * @param s 单条采样数据
 * @return DB_OK成功；非OK代表数据库操作出错
 * @note 每收到一笔采样调用一次；命中条件生成告警记录；条件消失自动恢复告警
 */
int alarm_check(const sensor_sample_t* s)
{
    int rc;
    if (!s)
        return DB_ERR_PARAM;

    // 遍历内存中全部告警规则
    for (int i = 0; i < g_rule_cnt; i++) {
        const alarm_rule_t* r = &g_rules[i];

        /* 规则是否适用于该设备：全局(device_id==0) 或 指定设备；不匹配则跳过这条规则 */
        if (r->device_id != 0 && r->device_id != s->device_id)
            continue;

        // 获取采样点对应字段的实际值
        double v = field_value(s, r->field);
        // 和阈值做比较，判断是否命中告警
        bool hit = compare(v, r->operator_, r->threshold);

        if (hit) {
            /* 已告警中则跳过，避免重复插入多条告警记录刷屏 */
            if (find_open(s->device_id, r->id) >= 0)
                continue;

            /* 触发新告警：组装告警记录结构体 */
            alarm_record_t rec;
            memset(&rec, 0, sizeof(rec));
            rec.device_id = s->device_id;
            rec.rule_id = r->id;
            rec.level = r->level;
            rec.trigger_ts = now_ts();
            rec.status = 0; // status=0 告警中
            snprintf(rec.description, sizeof(rec.description),
                "设备%d %s %s %.2f", s->device_id, r->field, r->operator_, r->threshold);

            // 调用storage接口，向数据库插入告警记录，插入成功会回填rec.id
            rc = db_alarm_insert(&rec);
            if (rc != DB_OK)
                return rc;

            // 把这条告警加入内存告警中集合，防止下一轮采样重复告警
            push_open(s->device_id, r->id, rec.id);
        }
        else {
            /* 未命中告警条件：如果之前处于告警中，则执行恢复操作 */
            int idx = find_open(s->device_id, r->id);
            if (idx >= 0) {
                // 更新数据库告警记录：status置1已恢复，填写恢复时间戳
                rc = db_alarm_update_status(g_open[idx].alarm_id, 1, now_ts());
                if (rc != DB_OK)
                    return rc;
                // 内存中移除这条告警中条目
                remove_open(idx);
            }
        }
    }
    return DB_OK;
}

/**
 * @brief 查询指定设备当前未恢复（告警中）的告警条数
 * @param device_id 目标设备id
 * @param count 输出参数，返回告警数量
 * @return DB_OK成功，其他错误码数据库错误
 */
int alarm_open_count(int device_id, int* count)
{
    // 直接调用storage底层统计接口
    return db_alarm_count_open(device_id, count);
}
