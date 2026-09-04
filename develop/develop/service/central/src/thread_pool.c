#include "../include/central_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* ========== 数据队列节点 ========== */
typedef struct data_node {
    ClientConnection conn;       // 连接信息（复制）
    DataPayload payload;         // 数据负载
    struct data_node *next;
} DataNode;

/* ========== 数据队列 ========== */
typedef struct {
    DataNode *head;
    DataNode *tail;
    int count;
    int max_count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int running;
} DataQueue;

/* ========== 线程池 ========== */
typedef struct {
    pthread_t *threads;
    int thread_count;
    DataQueue queue;
    // 回调函数
    on_data_received_t on_data;
    on_device_online_t on_online;
    on_device_offline_t on_offline;
    on_heartbeat_t on_heartbeat;
    int running;
} ThreadPool;

static ThreadPool g_pool;

/* ========== 队列操作 ========== */
static void queue_init(DataQueue *q, int max_count) {
    q->head = q->tail = NULL;
    q->count = 0;
    q->max_count = max_count;
    q->running = 1;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_push(DataQueue *q, ClientConnection *conn, DataPayload *payload) {
    DataNode *node = malloc(sizeof(DataNode));
    if (!node) return;
    
    // 复制数据
    memcpy(&node->conn, conn, sizeof(ClientConnection));
    memcpy(&node->payload, payload, sizeof(DataPayload));
    node->next = NULL;
    
    pthread_mutex_lock(&q->mutex);
    
    if (q->count >= q->max_count) {
        // 队列满，丢弃最老的数据
        DataNode *old = q->head;
        q->head = q->head->next;
        free(old);
        q->count--;
    }
    
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->count++;
    
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

static DataNode* queue_pop(DataQueue *q) {
    pthread_mutex_lock(&q->mutex);
    
    while (q->head == NULL && q->running) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    
    if (!q->running) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    
    DataNode *node = q->head;
    q->head = q->head->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    
    pthread_mutex_unlock(&q->mutex);
    return node;
}

static void queue_destroy(DataQueue *q) {
    q->running = 0;
    pthread_cond_broadcast(&q->cond);
    
    while (q->head) {
        DataNode *node = q->head;
        q->head = q->head->next;
        free(node);
    }
    
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

/* ========== 工作线程 ========== */
static void* worker_thread(void *arg) {
    ThreadPool *pool = (ThreadPool*)arg;
    
    while (pool->running) {
        DataNode *node = queue_pop(&pool->queue);
        if (!node) break;
        
        // 处理数据
        if (pool->on_data) {
            pool->on_data(&node->conn, &node->payload);
        }
        
        free(node);
    }
    return NULL;
}

/* ========== 对外接口 ========== */

// 初始化线程池
int thread_pool_init(int thread_count, int queue_size,
                     on_data_received_t on_data,
                     on_device_online_t on_online,
                     on_device_offline_t on_offline,
                     on_heartbeat_t on_heartbeat) {
    
    memset(&g_pool, 0, sizeof(g_pool));
    g_pool.thread_count = thread_count;
    g_pool.running = 1;
    g_pool.on_data = on_data;
    g_pool.on_online = on_online;
    g_pool.on_offline = on_offline;
    g_pool.on_heartbeat = on_heartbeat;
    
    queue_init(&g_pool.queue, queue_size);
    
    g_pool.threads = malloc(thread_count * sizeof(pthread_t));
    if (!g_pool.threads) return -1;
    
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&g_pool.threads[i], NULL, worker_thread, &g_pool);
    }
    
    printf("[INFO] 线程池初始化: %d 线程, 队列大小 %d\n", thread_count, queue_size);
    return 0;
}

// 提交数据到队列（由 central_server 调用）
void thread_pool_submit(ClientConnection *conn, DataPayload *payload) {
    if (!g_pool.running) return;
    queue_push(&g_pool.queue, conn, payload);
}

// 销毁线程池
void thread_pool_destroy(void) {
    g_pool.running = 0;
    g_pool.queue.running = 0;
    
    pthread_cond_broadcast(&g_pool.queue.cond);
    
    for (int i = 0; i < g_pool.thread_count; i++) {
        pthread_join(g_pool.threads[i], NULL);
    }
    
    queue_destroy(&g_pool.queue);
    free(g_pool.threads);
    
    printf("[INFO] 线程池已销毁\n");
}