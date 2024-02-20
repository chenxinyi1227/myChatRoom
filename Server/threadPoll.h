#ifndef __THREAD_POLL_H__
#define __THREAD_POLL_H__

#include <pthread.h>

/* 任务结构体 */
typedef struct task_t
{
    void *(*function)(void *);
    void *arg;
    struct task_t *next;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
}task_t;

/* 线程池结构体 */
typedef struct thread_poll_t 
{
    /* 任务队列 */
    task_t *taskQueue;        // 任务队列
    int queueCapacity;        // 任务队列容量
    int queueSize;            // 任务队列大小
    int queueFront;           // 队列头
    int queueRear;            // 队列尾

    /* 线程数组 */
    pthread_t *threadID;      // 线程id数组
    pthread_t managerID;      // 管理者线程id
    int maxSize;              // 最大线程数量
    int minSize;              // 最小线程数量
    int busySize;             // 忙线程数量
    int aliveSize;            // 存活线程数量
    int exitSize;             // 退出线程数量
    int shutdown;             // 线程池状态
    

    /* 互斥锁 */
    pthread_mutex_t mutex;
    pthread_cond_t notEmpty;
    pthread_cond_t notFull;
    pthread_mutex_t busyMutex;// 忙线程数量互斥锁
    
}
thread_poll_t;

/* 线程池初始化 */
int threadPollInit(thread_poll_t *thread_poll, int minSize, int maxSize, int queueCapacity);
/* 线程池添加任务 */
int threadPollAddTask(thread_poll_t *thread_poll, void *(*function)(void *), void *arg);
/* 线程池销毁 */
int threadPollDestroy(thread_poll_t *thread_poll);
/* 线程池启动 */
int threadPollStart(thread_poll_t *thread_poll);


#endif //__THREAD_POLL_H__