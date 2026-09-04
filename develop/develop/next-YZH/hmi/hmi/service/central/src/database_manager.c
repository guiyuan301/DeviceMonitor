#include "../include/database_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>

static sqlite3 *g_db = NULL;
static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

// 初始化数据库
int db_init(const char *db_path) {
    pthread_mutex_lock(&g_db_mutex);
    
    if (sqlite3_open(db_path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] 无法打开数据库: %s\n", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    // 创建设备状态表
    char *err_msg = NULL;
    const char *sql_device = 
        "CREATE TABLE IF NOT EXISTS devices ("
        "device_id INTEGER PRIMARY KEY,"
        "is_online INTEGER DEFAULT 0,"
        "last_update INTEGER);";
    
    if (sqlite3_exec(g_db, sql_device, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] 创建设备表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    // 创建生产数据表（历史回溯）
    const char *sql_data = 
        "CREATE TABLE IF NOT EXISTS production_data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id INTEGER,"
        "timestamp INTEGER,"
        "temperature REAL,"
        "humi INTEGER,"
        "status INTEGER,"
        "production INTEGER);";
        
    if (sqlite3_exec(g_db, sql_data, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] 创建数据表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    printf("[INFO] 数据库初始化成功\n");
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

// 关闭数据库
void db_close(void) {
    pthread_mutex_lock(&g_db_mutex);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
        printf("[INFO] 数据库已关闭\n");
    }
    pthread_mutex_unlock(&g_db_mutex);
}

// 插入测量数据（线程安全）
int db_insert_measurement(uint16_t device_id, const DataPayload *data) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    // 使用预编译语句防止 SQL 注入并提高性能
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO production_data "
                      "(device_id, timestamp, temperature, humi, status, production) "
                      "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[DB ERROR] SQL预处理失败: %s\n", sqlite3_errmsg(g_db));
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, device_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)data->timestamp);
    sqlite3_bind_double(stmt, 3, data->temperature / 100.0);
    sqlite3_bind_int(stmt, 4, data->humi);
    sqlite3_bind_int(stmt, 5, data->status);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)data->production);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&g_db_mutex);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB ERROR] 插入数据失败: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

// 更新设备在线状态（线程安全）
int db_update_device_status(uint16_t device_id, uint8_t status) {
    pthread_mutex_lock(&g_db_mutex);
    if (!g_db) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    const char *sql = "INSERT INTO devices (device_id, is_online, last_update) "
                      "VALUES (?, ?, ?) "
                      "ON CONFLICT(device_id) DO UPDATE SET "
                      "is_online=excluded.is_online, last_update=excluded.last_update;";
    
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, device_id);
    sqlite3_bind_int(stmt, 2, status);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)time(NULL));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&g_db_mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}