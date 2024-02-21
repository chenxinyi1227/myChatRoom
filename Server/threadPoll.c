#include "threadPoll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

/* 默认最大最小值 */
#define DEFAULT_MIN_THREAD_NUM 0
#define DEFAULT_MAX_THREAD_NUM 10
#define DEFAULT_QUEUE_MAX_SIZE 100
#define TIME_INTERVAL 5
#define DEFAULT_EXPANSION_SIZE 3
#define DEFAULT_REDUCTION_SIZE 3

/* 状态码 */
enum STATUS_CODE
{
    SUCCESS = 0,
    NULL_PTR,
    MALLOC_ERROR,
    ACCESS_INVAILD,
    THREAD_CREATE_ERROR,
    THREAD_JOIN_ERROR,
    THREAD_EXIT_ERROR,
    THREAD_MUTEX_ERROR,
    THREAD_COND_ERROR,
    MUTEX_DESTROY_ERROR,
    UNKNOWN_ERROR,
    
};

/* 静态函数前置声明 */
static void *threadHander(void *arg);
static void *mangerHander(void *arg);

/* 线程退出清除资源 */
static int threadExitClrResources(thread_poll_t *thread_poll)
{
    for(int idx = 0; idx < thread_poll->maxSize; idx++)
    {
        if(thread_poll->threadID[idx] == pthread_self())
        {
            thread_poll->threadID[idx] = 0;
            break;
        }
    }
    // pthread_exit(NULL);
    return SUCCESS;
}

/* 线程函数 */
static void *threadHander(void *arg)
{
    thread_poll_t *thread_poll = (thread_poll_t *)arg;
    while(1)
    {
        /* 加锁 */
        pthread_mutex_lock(&thread_poll->mutex);
        while(thread_poll->queueSize == 0 && thread_poll->shutdown != 1)
        {
            /* 等待一个条件变量被唤醒 */
            pthread_cond_wait(&thread_poll->notEmpty, &thread_poll->mutex);

        }
        if(thread_poll->exitSize > 0)
        {
            thread_poll->exitSize--;
            if(thread_poll->aliveSize > thread_poll->minSize)
            {
                
                /* 解锁 */
                pthread_mutex_unlock(&thread_poll->mutex);
                /* 释放资源 */
                threadExitClrResources(thread_poll);
                /* 退出 */
                pthread_exit(NULL);
            }
        }
        /* 出队 */
        task_t task = thread_poll->taskQueue[thread_poll->queueFront]; 
        thread_poll->queueFront = (thread_poll->queueFront + 1) % thread_poll->queueCapacity;
        /* 队列大小-1 */
        thread_poll->queueSize--;
        /* 解锁 */
        pthread_mutex_unlock(&thread_poll->mutex);
        /* 唤醒生产者 */
        pthread_cond_signal(&thread_poll->notFull);

        /* 为了提升性能，在创建一把只维护busySize的锁 */
        pthread_mutex_lock(&thread_poll->busyMutex);
        /* 增加busySize */
        thread_poll->busySize++;
        /* 解锁 */
        pthread_mutex_unlock(&thread_poll->busyMutex);

        /* 执行任务 */
        task.function(task.arg);
        /* 释放任务 */
        // free(task.arg);
        // task.arg = NULL;
        // free(task.function);
        // task.function = NULL;

        pthread_mutex_lock(&thread_poll->busyMutex);
        /* 减少busySize */
        thread_poll->busySize--;
        /* 解锁 */
        pthread_mutex_unlock(&thread_poll->busyMutex);

        

    }
}

/* 管理员线程 */
static void *mangerHander(void *arg)
{
    thread_poll_t *thread_poll = (thread_poll_t *)arg;
    while(!thread_poll->shutdown)
    {
        sleep(TIME_INTERVAL);

        /* 获取线程池信息 */
        pthread_mutex_lock(&thread_poll->mutex);
        int taskNum = thread_poll->queueSize;
        int aliveSize = thread_poll->aliveSize;
        pthread_mutex_unlock(&thread_poll->mutex);

        pthread_mutex_lock(&thread_poll->busyMutex);
        int busySize = thread_poll->busySize;
        pthread_mutex_unlock(&thread_poll->busyMutex);

        /* 扩容: 任务数>存活线程数 && 存活线程数<最大线程数  */
        if (taskNum > aliveSize && aliveSize < thread_poll->maxSize)
        {
            /* 一次扩大3个 */
            int count = 0;
            int ret = 0;
            pthread_mutex_lock(&thread_poll->mutex);
            for (int idx = 0; count < DEFAULT_EXPANSION_SIZE && idx < thread_poll->maxSize; idx++)
            {
                if (thread_poll->threadID[idx] == 0)
                {
                    ret = pthread_create(&thread_poll->threadID[idx], NULL, threadHander, thread_poll);
                    if (ret != 0)
                    {
                        perror("pthread_create error");
                        break; /* todo */
                    }
                    count++;
                    thread_poll->aliveSize++;
                }
            }
            pthread_mutex_unlock(&thread_poll->mutex);
        }

        
        /* 缩容: 忙线程数*2>存活线程数 && 存活线程数>最小线程数 */
        if ((busySize << 1) > aliveSize && aliveSize > thread_poll->minSize)
        {
            pthread_mutex_lock(&thread_poll->mutex);

            /* 设置退出线程的数量 */
            thread_poll->exitSize = DEFAULT_REDUCTION_SIZE;
            /* 唤醒所有等待线程 */
            pthread_cond_broadcast(&thread_poll->notEmpty);

            pthread_mutex_unlock(&thread_poll->mutex);
        }
    }
    /* 管理线程退出 */
    pthread_exit(NULL);
}

/* 线程池初始化 */
int threadPollInit(thread_poll_t *thread_poll, int minSize, int maxSize, int queueCapacity)
{
    if(thread_poll == NULL)
    {
        return NULL_PTR;
    }

    do
    {
        
        /* 判断合法性 */
        if(minSize < 0 || maxSize <= 0 || minSize > maxSize)
        {
            minSize = DEFAULT_MIN_THREAD_NUM;
            maxSize = DEFAULT_MAX_THREAD_NUM;
        }

        /* 更新线程池属性 */
        thread_poll->minSize = minSize;
        thread_poll->maxSize = maxSize;
        thread_poll->busySize = 0;
        

        /* 队列大小合法性 */
        if(queueCapacity <= 0)
        {
            queueCapacity = DEFAULT_QUEUE_MAX_SIZE;
        }
        thread_poll->queueCapacity = queueCapacity;
        thread_poll->queueSize = 0;
        thread_poll->queueFront = 0;
        thread_poll->queueRear = 0;
        thread_poll->taskQueue = (task_t *)malloc(sizeof(task_t) * queueCapacity);
        if(thread_poll->taskQueue == NULL)
        {
            perror("malloc error");
            break;
        }
        memset(thread_poll->taskQueue, 0, sizeof(task_t) * queueCapacity);
        

        /* 为线程ID分配空间 */
        thread_poll->threadID = (pthread_t *)malloc(sizeof(pthread_t) * maxSize);
        if(thread_poll->threadID == NULL)
        {
            perror("malloc error");
            return MALLOC_ERROR;
        }
        /* 清除脏数据 */
        memset(thread_poll->threadID, 0, sizeof(pthread_t) * maxSize);

        int ret = 0;
        /* 创建管理线程 */
        ret = pthread_create(&thread_poll->managerID, NULL, mangerHander, thread_poll);
        if(ret != 0)
        {
            perror("pthread_create error");
            break;
        }
        /* 创建线程 */
        for(int idx = 0; idx < minSize; idx++)
        {
            /* 判断ID号是否能够使用 */
            if(thread_poll->threadID[idx] == 0)
            {
                ret = pthread_create(&thread_poll->threadID[idx], NULL, threadHander, thread_poll);
                if(ret != 0)
                {
                    // perror("pthread_create error");
                    printf("pthread_create error\n");
                    
                    break;
                }
            }

        }
        if(ret != 0)
        {
            break;
        }
        /* 存活的线程数等于最小线程数 */
        thread_poll->aliveSize = minSize;
        /* 退出线程数 */
        thread_poll->exitSize = 0;
        /* 销毁标志 */
        thread_poll->shutdown = 0;

        /* 锁初始化 */
        pthread_mutex_init(&(thread_poll->mutex), NULL);
        pthread_mutex_init(&(thread_poll->busyMutex), NULL);
        /* 条件变量初始化 */
        if(pthread_cond_init(&(thread_poll->notEmpty), NULL) != 0 || pthread_cond_init(&(thread_poll->notFull), NULL) != 0)
        {
            perror("pthread_cond_init error");
            break;
        }

        return SUCCESS;
    }while(0);

    /* 程序执行到此，上面一定有bug */
    /* 回收堆空间 */
    if(thread_poll->taskQueue != NULL)
    {
        free(thread_poll->taskQueue);
        thread_poll->taskQueue = NULL;
    }

    /* 回收线程资源 */
    for(int idx = 0; idx < thread_poll->minSize; idx++)
    {
        if(pthread_join(thread_poll->threadID[idx], NULL) != 0)
        {
            pthread_join(thread_poll->threadID[idx], NULL);
        }
    }
    if(thread_poll->threadID != NULL)
    {
        free(thread_poll->threadID);
        thread_poll->threadID = NULL;
    }
    /* 回收管理者资源 */
    if(thread_poll->managerID != 0)
    {
        pthread_join(thread_poll->managerID, NULL);
    }


    /* 释放锁资源 */
    if(pthread_mutex_destroy(&(thread_poll->mutex)) != 0)
    {
        perror("pthread_mutex_destroy error");
        return MUTEX_DESTROY_ERROR;
    }
    if(pthread_mutex_destroy(&(thread_poll->busyMutex)) != 0)
    {
        perror("pthread_mutex_destroy error");
        return MUTEX_DESTROY_ERROR;
    }
    /* 释放条件变量资源 */
    pthread_cond_destroy(&(thread_poll->notEmpty));
    pthread_cond_destroy(&(thread_poll->notFull));

    return UNKNOWN_ERROR;
}

/* 线程池添加任务 */
int threadPollAddTask(thread_poll_t *thread_poll, void *(*function)(void *), void *arg)
{
    if(thread_poll == NULL || function == NULL || arg == NULL)
    {
        return NULL_PTR;
    }

    /* 加锁 */
    pthread_mutex_lock(&thread_poll->mutex);
    /* 任务队列满了 */
    // while(thread_poll->aliveSize == thread_poll->queueCapacity)
    while(thread_poll->queueSize >= thread_poll->queueCapacity)
    {
        pthread_cond_wait(&thread_poll->notFull, &thread_poll->mutex);
    }
    /* 到此位置说明队列有空位 */
    /* 添加任务 */
    thread_poll->taskQueue[thread_poll->queueRear].function = function;
    thread_poll->taskQueue[thread_poll->queueRear].arg = arg;
    /* 队列尾指针后移 */
    thread_poll->queueRear = (thread_poll->queueRear + 1) % thread_poll->queueCapacity;
    /* 队列大小+1 */
    thread_poll->queueSize++;
    /* 解锁 */
    pthread_mutex_unlock(&thread_poll->mutex);

    /* 唤醒消费者 */
    pthread_cond_signal(&thread_poll->notEmpty);

    return SUCCESS;
}

/* 线程池销毁 */
int threadPollDestroy(thread_poll_t *thread_poll)
{

    thread_poll->shutdown = 1;

    /* 回收管理者线程 */
    pthread_join(thread_poll->managerID, NULL);
    
    /* 唤醒所有线程 */
    pthread_cond_broadcast(&thread_poll->notEmpty);

    /* 等待所有线程退出 */
    for (int idx = 0; idx < thread_poll->maxSize; idx++)
    {
        if (thread_poll->threadID[idx] != 0)
        {
            pthread_join(thread_poll->threadID[idx], NULL);
        }
    }

    return SUCCESS;
}