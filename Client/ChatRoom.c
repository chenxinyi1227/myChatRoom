#include "ChatRoom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <json-c/json.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* 状态码 */
enum STATUS_CODE
{
    SUCCESS = 0,        // 成功
    NULL_PTR = -1,      // 空指针
    MALLOC_ERROR = -2,  // 内存分配失败
    ILLEGAL_ACCESS = -3,// 非法访问
    UNDERFLOW = -4,     // 下溢
    OVERFLOW = -5,      // 上溢
    SOCKET_ERROR = -6,  // socket错误
    CONNECT_ERROR = -7, // 连接错误
    SEND_ERROR = -8,    // 发送错误
    RECV_ERROR = -9,    // 接收错误
    FILE_ERROR = -10,   // 文件错误
    JSON_ERROR = -11,   // json错误
    OTHER_ERROR = -12,  // 其他错误

};

#define SERVER_PORT 8888        // 服务器端口号,暂定为8888
#define SERVER_IP "172.18.188.222"   // 服务器ip,暂定为本机ip
#define NAME_SIZE 10            // 用户名长度
#define PASSWORD_SIZE 20        // 密码长度
#define MAX_FRIEND_NUM 10       // 最大好友数量
#define MAX_GROUP_NUM 10        // 最大群组数量
#define MAX_GROUP_MEMBERS_NUM 20// 最大群组成员数量
#define CONTENT_SIZE 1024       // 信息内容长度
#define PATH_SIZE 256           // 文件路径长度
#define BUFFER_SIZE 1024        //数组大小
#define MAX_PATH    1024        //路径大小

/* 定义结构体来保存接收参数 */
typedef struct 
{
    int sockfd;
    char *path;
    json_object *friends;
    json_object *groups;
} RecvArgs;

/* 声明全局变量 */
/* 互斥锁 */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
/* 接收标识 */
int g_recv_flag = 0;

/* 静态声明 */
/* 登录成功的主界面 */
static int ChatRoomMain(int fd, json_object *json);
/* 退出登录 */
static int ChatRoomLogout(int fd, const char *username);
/* 将未读消息写入本地文件 */
static int ChatRoomSaveUnreadMsg(json_object *json, const char *path);

/* 发送json到服务器 */
static int SendJsonToServer(int fd, const char *json)
{
    printf("开始发送json\n");
    int ret = 0;
    int len = strlen(json);
    ret = send(fd, json, len, 0);
    if (ret < 0)
    {
        perror("send error");
    }
    return SUCCESS;
}
/* 接收json */
static int RecvJsonFromServer(int fd,  char *json)
{

    printf("开始接收json\n");
    int ret = recv(fd, json, CONTENT_SIZE, 0);
    if (ret < 0)
    {
        perror("recv error");
        return ret;
    }
    // printf("json:%s\n",json);
    printf("接收json成功\n");
    return SUCCESS;
}

/* 拼接路径 */
static int JoinPath(char *path, const char *dir, const char *filename)
{
    int ret = 0;
    if (path == NULL || dir == NULL || filename == NULL)
    {
        return NULL_PTR;
    }
    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, filename);
    return SUCCESS;
}

//获取文件目录下的文件
int getDirectoryFiles(const char* directory_path) 
{
    DIR* directory = opendir(directory_path);
    struct dirent* entry;

    if (directory == NULL) 
    {
        printf("无法打开目录\n");
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) 
    {
        printf("%s\n", entry->d_name);
    }
    closedir(directory);
    return SUCCESS;
}

/* 聊天室初始化 */
int ChatRoomInit()
{
    /* 初始化锁 */
    pthread_mutex_init(&mutex, NULL);

    /* 创建 */
    if(access("./usersData", F_OK) == -1)
    {
        if (mkdir("./usersData", 0777) == -1)
        {
            perror("mkdir error");
            return MALLOC_ERROR;
        }
    }
    /* 初始化与服务器的连接 */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket error");
        return SOCKET_ERROR;
    }
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    int ret = connect(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (ret == -1)
    {
        perror("connect error");
        return CONNECT_ERROR;
    }

    printf("欢迎使用网络聊天室\n");
    sleep(1);
    while(1)
    {
        system("clear");
        printf("请输入要进行的功能:\na.登录\nb.注册\n其他.退出聊天室\n");
        char ch;
        while ((ch = getchar()) == '\n');   // 读取一个非换行的字符
        while ((getchar()) != '\n');        // 吸收多余的字符
        printf("ch:%d\n",ch);
        switch (ch)
        {
            case 'a':
                ChatRoomLogin(fd);
                break;
            case 'b':
                ChatRoomRegister(fd);
                break;
            default:
                ChatRoomExit();
                close(fd);
                return SUCCESS;
        }
    }

}

/* 聊天室退出 */
int ChatRoomExit()
{
    /* todo... */
    printf("感谢您的使用\n");
    return 0;
}

/* 聊天室注册 */
int ChatRoomRegister(int sockfd)
{
    char name[NAME_SIZE] = {0};
    char password[PASSWORD_SIZE] = {0};

    printf("注册\n");
    printf("请输入账号:");
    scanf("%s", name);
    /* 不显示输入的密码 */
    strncpy(password, getpass("请输入密码:"), PASSWORD_SIZE);

    /* 注册信息转化为json，发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("register"));
    json_object_object_add(jobj, "name", json_object_new_string(name));
    json_object_object_add(jobj, "password", json_object_new_string(password));
    const char *json = json_object_to_json_string(jobj);

    /* 发送json */
    /*
        发送给服务器的信息：
            请求类型，账号，密码
    */
    SendJsonToServer(sockfd, json);


    /* 等待服务器响应 */
    printf("注册中 ");
    /*
        预期接收到的服务器信息：
        receipt:success/fail
        (success)
        name:自己的ID
        friends:
            name:待处理消息数
        groups:
            name:待处理消息数
        (fail)
        reason:失败原因

    */
    char retJson[CONTENT_SIZE] = {0};
    RecvJsonFromServer(sockfd, retJson);

    json_object *jreceipt = json_tokener_parse(retJson);
    if (jreceipt == NULL)
    {
        printf("注册失败\n");
        json_object_put(jreceipt);
        json_object_put(jobj);
        jreceipt = NULL;
        jobj = NULL;
        return JSON_ERROR;
    }

    const char *receipt = json_object_get_string(json_object_object_get(jreceipt,"receipt"));
    if (strcmp(receipt, "success") == 0)
    {
        printf("注册成功\n");
        json_object_put(jobj);
        jobj = NULL;
        json_object_object_del(jreceipt, "receipt");    // 删除掉多余的回执数据
        /* 初始化好友列表和群组列表 */
        ChatRoomMain(sockfd,jreceipt);  
    }
    else
    {
        const char *reason = json_object_get_string(json_object_object_get(jreceipt,"reason"));
        printf("注册失败:%s\n",reason);
        json_object_put(jreceipt);
        json_object_put(jobj);
        jreceipt = NULL;
        jobj = NULL;
    }

    return SUCCESS;
}

/* 聊天室登录 */
int ChatRoomLogin(int sockfd)
{
    char name[NAME_SIZE] = {0};
    char password[PASSWORD_SIZE] = {0};

    printf("登录\n");
    printf("请输入账号:");
    scanf("%s", name);
    /* 不显示输入的密码 */
    strncpy(password, getpass("请输入密码:"), PASSWORD_SIZE);
    /* 登录信息转化为json，发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("login"));
    json_object_object_add(jobj, "name", json_object_new_string(name));
    json_object_object_add(jobj, "password", json_object_new_string(password));
    const char *json = json_object_to_json_string(jobj);

    /*
        发送给服务器的信息：
            请求类型，账号，密码
    */
    SendJsonToServer(sockfd, json);

    /* 等待服务器响应 */
    printf("登录中 ");
    char retJson[CONTENT_SIZE] = {0};
    RecvJsonFromServer(sockfd, retJson);
    /*
        预期接收到的服务器信息：
        receipt:success/fail
        name:自己的ID
        friends:
            name:待处理消息数
        groups:
            name:待处理消息数

    */
    json_object *jreceipt = json_tokener_parse(retJson);

    if (jreceipt == NULL)
    {
        printf("登录失败\n");
        json_object_put(jreceipt);
        json_object_put(jobj);
        jreceipt = NULL;
        jobj = NULL;
        return JSON_ERROR;
    }

    const char *receipt = json_object_get_string(json_object_object_get(jreceipt,"receipt"));
    if (strcmp(receipt, "success") == 0)
    {
        printf("登录成功\n");
        sleep(1);
        json_object_put(jobj);
        jobj = NULL;
        json_object_object_del(jreceipt, "receipt");    // 删除掉多余的回执数据
        ChatRoomMain(sockfd,jreceipt);
    }
    else
    {
        const char *reason = json_object_get_string(json_object_object_get(jreceipt,"reason"));
        printf("登录失败:%s\n",reason);
        json_object_put(jreceipt);
        json_object_put(jobj);
        jreceipt = NULL;
        jobj = NULL;
        return SUCCESS;
    }
    return SUCCESS;
}


/* 添加好友 */
int ChatRoomAddFriend(int sockfd, const char *name, json_object *friends, const char *username)
{
    /* 添加好友信息转化为json，发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("addfriend"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "friend", json_object_new_string(name));
    const char *json = json_object_to_json_string(jobj);
    /* 发送json */
    /*
        发送给服务器的信息：
            type:addfriend
            name:自己的ID
            friend:好友ID
    */
    SendJsonToServer(sockfd, json);

    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;
    /* 将好友加入好友列表 */
    json_object_object_add(friends, name, json_object_new_int(0));
    /* 反馈 */
    printf("好友申请已发送\n");
    return SUCCESS;
}

/* 打印好友列表 */
static int ChatRoomPrintFriends(json_object *friends)
{
    printf("好友列表:\n");
    int jsonLen = json_object_object_length(friends);
    
    if(jsonLen == 0)
    {
        printf("暂无好友\n");
        return SUCCESS;
    }
    else
    {
        json_object_object_foreach(friends, key, value)
        {
            const char *name = key;
            const int messages_num = json_object_get_int(value);
            if(messages_num > 0)
            {
                printf("%s(%d)\n", name, messages_num);
            }
            else
            {
                printf("%s\n", name);
            }
        }
        
    }
    return SUCCESS;
}

/* 显示好友 */
int ChatRoomShowFriends(int sockfd, json_object* friends, const char *username, const char * path)
{

    while(1)
    {   
        system("clear");
        if (ChatRoomPrintFriends(friends) != SUCCESS)
        {
            return SUCCESS;
        }
        printf("a.添加好友\nb.删除好友\nc.私聊\nd.退出\n其他.返回上一级\n");
        char ch;
        char name[NAME_SIZE] = {0};
        while ((ch = getchar()) == '\n');   // 读取一个非换行的字符
        while ((getchar()) != '\n');        // 吸收多余的字符
        switch (ch)
        {
            case 'a':
            {
                printf("请输入要添加的好友:");
                scanf("%s", name);
                ChatRoomAddFriend(sockfd, name, friends, username);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'b':
            {
                printf("请输入要删除的好友:");
                scanf("%s", name);
                /* 判断是否存在好友 */
                if(json_object_object_get(friends, name) == NULL)
                {
                    printf("好友不存在\n");
                    break;
                }
                ChatRoomDelFriend(sockfd, name, friends, username);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'c':
            {
                printf("请输入要私聊的好友:");
                scanf("%s", name);
                /* 判断是否存在好友 */
                if(json_object_object_get(friends, name) == NULL)
                {
                    printf("好友不存在\n");
                    break;
                }
                /* 创建私聊的本地聊天记录文件 */
                char privateChatRecord[PATH_SIZE] = {0};
                JoinPath(privateChatRecord, path, name);
                ChatRoomPrivateChat(sockfd, name, friends,username,privateChatRecord);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'd':
            {
                /* todo... */
                break;
            }
            default:
                return SUCCESS;
        }
    }
    return SUCCESS;
}

/* 删除好友 */
int ChatRoomDelFriend(int sockfd, const char *name, json_object *friends, const char *username)
{
    /* 删除好友信息转化为json,发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("delfriend"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "friend", json_object_new_string(name));
    const char *json = json_object_to_json_string(jobj);
    /* 发送json */
    /*
        发送给服务器的信息：
            type:delfriend
            name:自己的ID
            friend:好友ID
    */
    SendJsonToServer(sockfd, json);

    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;
    /* 将好友删除好友列表 */
    json_object_object_del(friends, name);
    /* 反馈 */
    printf("好友删除成功\n");
    return SUCCESS;
}

/* 私聊 */
int ChatRoomPrivateChat(int sockfd, const char *name, json_object *friends, const char *username, const char * path)
{
    printf("path:%s\n",path);
    /* 加锁 */
    pthread_mutex_lock(&mutex);
    /* 打开私聊的本地聊天记录文件 */
    FILE *fp = fopen(path, "a+");
    if(fp == NULL)
    {
        printf("打开文件失败\n");
        return ILLEGAL_ACCESS;
    }
    /* 输出聊天记录 */
    char line[CONTENT_SIZE] = {0};
    printf("私聊记录:\n");
    while(fgets(line,  CONTENT_SIZE, fp) != NULL)
    {
        printf("%s", line);
        memset(line, 0, CONTENT_SIZE);
    }
    fclose(fp);
    /* 解锁 */
    pthread_mutex_unlock(&mutex);

    /* 未读消息置零 */
    json_object_object_add(friends, name, json_object_new_int(0));
        

    char message[CONTENT_SIZE] = {0};

    printf("请输入要私聊的内容:");
    /* 清空缓存区 */
    while ((getchar()) != '\n');
    /* 使用 fgets 读取整行输入 */
    if (fgets(message, sizeof(message), stdin) == NULL) 
    {
        perror("fgets error");
        exit(EXIT_FAILURE);
    }

    /* 去掉输入字符串末尾的换行符 */
    size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') 
    {
        message[len - 1] = '\0';
    }

    /* 如果输入是空行，表示用户按下回车，退出私聊 */
    if (strcmp(message, "") == 0) 
    {
        return SUCCESS;
    }
    
    /* 获取时间 */
    time_t now;
    struct tm *tm;
    static char time_str[20] = {0};
    time(&now);
    tm = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
    /* 加锁 */
    pthread_mutex_lock(&mutex);
    /* 将消息写入文件 */
    fp = fopen(path, "a+");
    if(fp == NULL)
    {
        printf("打开文件失败\n");
        return ILLEGAL_ACCESS;
    }
    fprintf(fp, "[%s] %s:\n%s\n", username, time_str, message);
    /* 释放fp */
    fclose(fp);
    /* 解锁 */
    pthread_mutex_unlock(&mutex);

    
    /* 私聊信息转化为json，发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("private"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "friendName", json_object_new_string(name));
    json_object_object_add(jobj, "message", json_object_new_string(message));
    const char *json = json_object_to_json_string(jobj);
    /*
        发送给服务器的信息：
            type：private
            name: 用户名
            friendName：好友名
            message：私聊内容
    */
    SendJsonToServer(sockfd, json);
    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;
    return SUCCESS;
    
}

/* 接收消息 */
static void* ChatRoomRecvMsg(void* args)
{
    RecvArgs *recvArgs = (RecvArgs*)args;
    int sockfd = recvArgs->sockfd;
    const char *path = recvArgs->path;
    json_object *friends = recvArgs->friends;
    json_object *groups = recvArgs->groups;
    /*
        预期接收到的服务器信息：
            type:private/group
            name:发信人
            toname:收信人
            message:消息内容
            time:发送时间
    */
    /* 线程分离 */
    pthread_detach(pthread_self());
    while(g_recv_flag)
    {
        /* 接收服务器信息 */
        char retJson[1024] = {0};
        RecvJsonFromServer(sockfd, retJson);
        json_object *jobj = json_tokener_parse(retJson);
        if (jobj == NULL)
        {
            printf("接收消息失败\n");
            continue;
        }
        /* 获取type */
        json_object *typeJson = json_object_object_get(jobj, "type");
        if (typeJson != NULL)
        {
            const char *type = json_object_get_string(typeJson);

            if(strcmp(type, "createGroupChat") == 0)
            {
                /* 创群回执 */
                json_object *receipt = json_object_object_get(jobj, "receipt");
                if(receipt == NULL)
                {
                    printf("接收消息失败, 未接收到回执\n");
                }
                const char *receiptStr = json_object_get_string(receipt);
                if(strcmp(receiptStr, "success") == 0)
                {
                    /* 创群成功 */
                    printf("创群成功\n");
                }
                else
                {
                    /* 创群失败 */
                    json_object *reason = json_object_object_get(jobj, "reason");
                    if(reason == NULL)
                    {
                        printf("接收消息失败, 未接收到失败原因\n");
                        continue;
                    }
                    const char *reasonStr = json_object_get_string(reason);
                    printf("创群失败, 失败原因:%s\n", reasonStr);
                }
                continue;
            }

            /* 获取发送人 */
            json_object *nameJson = json_object_object_get(jobj, "name");
            if (nameJson == NULL)
            {
                printf("接收消息失败,未接收到发信人\n");
                continue;
            }
            const char *name = json_object_get_string(nameJson);
            /* 获取消息 */
            json_object *messageJson = json_object_object_get(jobj, "message");
            if (messageJson == NULL)
            {
                printf("接收消息失败,未接收到消息\n");
                continue;
            }
            const char *message = json_object_get_string(messageJson);
            /* 获取时间 */
            json_object *timeJson = json_object_object_get(jobj, "time");
            if (timeJson == NULL)
            {
                printf("接收消息失败,未接收到时间\n");
                continue;
            }
            const char *time = json_object_get_string(timeJson);
            /* 判断请求类型 */
            if(strcmp(type, "private") == 0)
            {
                /* 私聊 */
                /* 保存消息 */

                /* 拼接路径 */
                char privateChatRecordPath[PATH_SIZE] = {0};
                JoinPath(privateChatRecordPath, path, name);
                /* 未读消息数+1 */
                const int unread = json_object_get_int(json_object_object_get(friends, name));
                json_object_object_add(friends, name, json_object_new_int(unread + 1));
                /* 加锁 */
                pthread_mutex_lock(&mutex);
                /* 打开私聊的本地聊天记录文件 */
                FILE *fp = fopen(privateChatRecordPath, "a+");
                if(fp == NULL)
                {
                    printf("打开文件失败\n");
                    continue;
                }
                /* 写入聊天记录 */
                fprintf(fp, "[%s] %s:\n%s\n", name, time, message);
                fclose(fp);
                /* 解锁 */
                pthread_mutex_unlock(&mutex);
            }
            else if(strcmp(type, "groupchat") == 0)
            {
                /* 群聊 */
                /* 获取群名称 */
                json_object *groupNameJson = json_object_object_get(jobj, "groupName");
                if (groupNameJson == NULL)
                {
                    printf("接收消息失败, 未接收到群名称\n");
                    continue;
                }
                const char *groupName = json_object_get_string(groupNameJson);
                char privateChatRecordPath[PATH_SIZE] = {0};
                JoinPath(privateChatRecordPath, path, groupName);
                /* 未读消息数+1 */
                const int unread = json_object_get_int(json_object_object_get(friends, groupName));
                json_object_object_add(friends, groupName, json_object_new_int(unread + 1));
                /* 加锁 */
                pthread_mutex_lock(&mutex);
                /* 打开群聊的本地聊天记录文件 */
                FILE *fp = fopen(privateChatRecordPath, "a+");
                if(fp == NULL)
                {
                    printf("打开文件失败\n");
                    continue;
                }
                /* 写入聊天记录 */
                fprintf(fp, "[%s] %s:\n%s\n", name, time, message);
                fclose(fp);
                /* 解锁 */
                pthread_mutex_unlock(&mutex);
            }
            continue;

        }
        /* 获取 receipt*/
        json_object *receiptJson = json_object_object_get(jobj, "receipt");
        if (receiptJson != NULL)
        {
            const char *receipt = json_object_get_string(receiptJson);
            /* 处理 receipt */
            if(strcmp(receipt, "success") == 0)
            {
                continue;
            }
            if(strcmp(receipt, "fail") == 0)
            {
                /* 获取reason */
                json_object *reasonJson = json_object_object_get(jobj, "reason");
                if (reasonJson == NULL)
                {
                    printf("接收消息失败,未接收到回执信息\n");
                    continue;
                }
                const char *reason = json_object_get_string(reasonJson);
                printf("回执信息:%s\n", reason);
                continue;

            }
            /* todo... 有其他再说 */
        }
    }
    return NULL;
}

/* 发起群聊 */
int ChatRoomAddGroupChat(int sockfd, const char *groupname, json_object *groups, const char *username)
{
    /* 创建json */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("createGroupChat"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "groupName", json_object_new_string(groupname));
    const char *json = json_object_to_json_string(jobj);
    printf("json:%s\n", json);
    /*
        发送给服务器的信息：
            type：createGroupChat
            name: 用户名
            groupName：群名
    */
    SendJsonToServer(sockfd, json);
    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;
    return SUCCESS;
}

/* 打印群组 */
static int ChatRoomPrintGroups(json_object *groups)
{
    printf("群组列表:\n");
    int jsonLen = json_object_object_length(groups);

    if(jsonLen == 0)
    {
        printf("暂无群组\n");
    }
    else
    {
        json_object_object_foreach(groups, key, value)
        {
            const char *name = key;
            const int messages_num = json_object_get_int(value);
            if(messages_num > 0)
            {
                printf("%s(%d)\n", name, messages_num);
            }
            else
            {
                printf("%s\n", name);
            }
        }
        
    }
    return SUCCESS;
}
/* 显示群聊列表 */
int ChatRoomShowGroupChat(int sockfd, json_object *groups, const char *username, const char *path)
{
    while(1)
    {

        if(ChatRoomPrintGroups(groups) != SUCCESS)
        {
            return SUCCESS;
        }
        printf("a.加入群组\nb.退出群组\nc.群聊\nd.创建群聊\n其他.返回上一级\n");
        char ch;
        char name[NAME_SIZE] = {0};
        while ((ch = getchar()) == '\n');   // 读取一个非换行的字符
        while ((getchar()) != '\n');        // 吸收多余的字符
        switch (ch)
        {
            case 'a':
            {
                printf("请输入要加入的群组:");
                scanf("%s", name);
                ChatRoomAddFriend(sockfd, name, groups, username);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'b':
            {
                printf("请输入要退出的群组:");
                scanf("%s", name);
                ChatRoomDelFriend(sockfd, name, groups, username);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'c':
            {
                /* 群聊 */
                printf("请输入要群聊的群组:");
                scanf("%s", name);
                ChatRoomGroupChat(sockfd, name, groups, username, path);
                memset(name, 0, NAME_SIZE);
                break;
            }
            case 'd':
            {
                /* 创建群聊 */
                printf("请输入要创建的群组:");
                scanf("%s", name);
                ChatRoomAddGroupChat(sockfd, name, groups, username);
                break;
            }
            default:
                return SUCCESS;
        }
    }
}

/* 群聊 */
int ChatRoomGroupChat(int sockfd, const char *name, json_object *groups, const char *username, const char *path)
{
    /* 判断群组是否存在 */
    json_object *groupJson = json_object_object_get(groups, name);
    if(groupJson == NULL)
    {
        printf("群组不存在\n");
        return ILLEGAL_ACCESS;
    }
    /* 拼接路径 */
    char groupChatRecordPath[PATH_SIZE] = {0};
    JoinPath(groupChatRecordPath, path, name);
    /* 加锁 */
    pthread_mutex_lock(&mutex);
    /* 打开群聊的本地聊天记录文件 */
    FILE *fp = fopen(groupChatRecordPath, "a+");
    if(fp == NULL)
    {
        printf("打开文件失败\n");
        return MALLOC_ERROR;
    }
    /* 输出群聊记录 */
    char line[CONTENT_SIZE] = {0};
    while(fgets(line, CONTENT_SIZE, fp) != NULL)
    {
        printf("%s", line);
        memset(line, 0, CONTENT_SIZE);
    }
    fclose(fp);
    /* 解锁 */
    pthread_mutex_unlock(&mutex);

    /* 未读消息置零 */
    json_object_object_add(groups, name, json_object_new_int(0));

    char message[CONTENT_SIZE] = {0};
    printf("请输入要发送的消息:");
    /* 清空缓存区 */
    while ((getchar()) != '\n');
    /* 使用 fgets 读取整行输入 */
    if (fgets(message, sizeof(message), stdin) == NULL) 
    {
        perror("fgets error");
        exit(EXIT_FAILURE);
    }

    /* 去掉输入字符串末尾的换行符 */
    size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') 
    {
        message[len - 1] = '\0';
    }

    /* 如果输入是空行，表示用户按下回车，退出私聊 */
    if (strcmp(message, "") == 0) 
    {
        return SUCCESS;
    }
    /* 获取时间 */
    time_t now;
    struct tm *tm;
    static char time_str[20] = {0};
    time(&now);
    tm = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
    /* 加锁 */
    pthread_mutex_lock(&mutex);
    /* 将消息写入文件 */
    fp = fopen(groupChatRecordPath, "a+");
    if(fp == NULL)
    {
        printf("打开文件失败\n");
        return ILLEGAL_ACCESS;
    }
    fprintf(fp, "[%s] %s:\n%s\n", username, time_str, message);
    fclose(fp);
    /* 解锁 */
    pthread_mutex_unlock(&mutex);

    /* 群聊信息转化为json,发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("groupchat"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "groupName", json_object_new_string(name));
    json_object_object_add(jobj, "message", json_object_new_string(message));
    const char *json_str = json_object_to_json_string(jobj);
    if(json_str == NULL)
    {
        printf("json_object_to_json_string error\n");
        return JSON_ERROR;
    }
    SendJsonToServer(sockfd, json_str);
    /* 释放json */
    json_object_put(jobj);
    jobj = NULL;
    return SUCCESS;

}

//上传文件
static int fileUpload(int sockfd)
{   
    //将需要上传的文件名传递给服务器
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("fileUpload"));
    const char *json = json_object_to_json_string(jobj);
    SendJsonToServer(sockfd, json);
    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;

    char src_path[MAX_PATH] = "/home/chatRoom/chatRoom/Client"; // 源文件路径
  
    printf("请输入要上传的文件名:");
    char dest_dir[MAX_PATH] = {0}; // 目标目录路径
    scanf("%s", dest_dir);

    char dest_path[MAX_PATH * 2] = {0}; // 目标文件路径
    JoinPath(dest_path, src_path, dest_dir);
    printf("dest_path:%s\n", dest_path);
    sleep(1);

    // 发送文件名
    if (send(sockfd, dest_dir, strlen(dest_dir) + 1, 0) < 0)
    {
        perror("发送文件名失败");
        exit(EXIT_FAILURE);
    }

    // 打开文件
    int file_fd = open(dest_path, O_RDONLY);
    if (file_fd < 0) 
    {
        perror("打开文件失败");
        exit(EXIT_FAILURE);
    }
    printf("打开文件成功\n");
    sleep(1);


    //读取文件内容并发送
    size_t bytes_read;
    char buffer[BUFFER_SIZE] = {0};
    while((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) 
    {
        if (send(sockfd, buffer, bytes_read, 0) < 0) 
        {
            perror("发送文件内容失败");
            close(file_fd);
            exit(EXIT_FAILURE);
        }
        memset(buffer, 0, sizeof(buffer)); // 清空buffer，准备下一次读取
    }
   
    if (bytes_read < 0) 
    {
        perror("读取文件内容失败");
        close(file_fd);
        exit(EXIT_FAILURE);
    }
    printf("文件上传成功\n");
    sleep(3);
    close(file_fd);
    return SUCCESS;

}

//下载文件
static int fileDownload()
{
   
}

/* 显示文件 */
int ChatRoomShowFile(int sockfd, json_object* friends, const char *username, const char * path)
{
    while(1)
    {   
        system("clear");
        printf("\na.查看文件\nb.上传文件\n其他:返回上一级\n");
        char ch;
        while ((ch = getchar()) == '\n');   // 读取一个非换行的字符
        while ((getchar()) != '\n');        // 吸收多余的字符
        printf("ch:%d\n",ch);
        switch (ch)
        {
            case 'a':
                getDirectoryFiles(path);
                break;
            case 'b':
                fileUpload(sockfd);
                break;
            default:
                return SUCCESS;
        }
    }
    return SUCCESS;
}

/* 退出群聊 */
int ChatRoomExitGroupChat(int sockfd, const char *groupname, const char *username)
{
    /* 退出群聊信息转化为json,发送给服务器 */
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("exitGroupChat"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    json_object_object_add(jobj, "groupName", json_object_new_string(groupname));
    const char *json = json_object_to_json_string(jobj);
    printf("json:%s\n", json);
    /*
        发送给服务器的信息：
            type：exitGroupChat
            name: 用户名
            groupName：群名
    */
    SendJsonToServer(sockfd, json);
    /* 释放jobj */
    json_object_put(jobj);
    jobj = NULL;

    /* 反馈 */
    printf("退出群聊成功\n");
    return SUCCESS;
}


/* 登录成功的主界面 */
static int ChatRoomMain(int fd, json_object *json)
{
    

    /* 用户名 */
    json_object *usernameJson = json_object_object_get(json, "name");
    if(usernameJson == NULL)
    {
        printf("json_object_object_get error\n");
        return JSON_ERROR;
    }
    const char *username = json_object_get_string(usernameJson);
    
    /* 创建用户本地数据目录 */
    char path[PATH_SIZE] = {0};
    JoinPath(path, "./usersData", username);
    if(access(path, F_OK) == -1)
    {
        if (mkdir(path, 0777) == -1)
        {
            perror("mkdir error");
            return MALLOC_ERROR;
        }
    }
    /* 好友列表 */
    json_object * friends = json_object_object_get(json, "friends");
    const char *friend = json_object_get_string(friends);
    printf("friend:%s\n",friend);
    /* 群组列表 */
    json_object * groups = json_object_object_get(json, "groups");
    const char *group = json_object_get_string(groups);
    printf("group:%s\n",group);

    /* 处理可能有的未读消息 */
    /* 未读消息格式
        messages:[
            {
                sender_name:xxx,
                message:xxx,
                send_time:xxx
            };
            {
                重复上文
            }
        ]
    */
    json_object *frinend_messages = json_object_object_get(json, "frinend_messages");
    ChatRoomSaveUnreadMsg(frinend_messages, path);
    json_object *group_messages = json_object_object_get(json, "group_messages");
    ChatRoomSaveUnreadMsg(group_messages, path);
    

    /* 开启接收 */
    pthread_t tid;
    g_recv_flag = 1;
    RecvArgs recvArgs;
    recvArgs.sockfd = fd;
    recvArgs.path = path;
    recvArgs.friends = friends;                                                        
    recvArgs.groups = groups;
    pthread_create(&tid, NULL, ChatRoomRecvMsg, (void *)&recvArgs);


    while(1)
    {
        system("clear");
        /* 显示好友列表和群组列表 */
        printf("a.显示好友列表\nb.显示群聊列表\nc.显示文件列表\ne.退出登录\n其他无效\n");
        char ch;
        while ((ch = getchar()) == '\n');   // 读取一个非换行的字符
        while ((getchar()) != '\n');        // 吸收多余的字符
        switch (ch)
        {
            case 'a':
                ChatRoomShowFriends(fd, friends,username, path);
                break;
            case 'b':
                ChatRoomShowGroupChat(fd, groups,username,path);
            case 'c'://显示文件列表
                ChatRoomShowFile(fd, groups,username,path);
                break;
            case 'e':
                printf("退出登录\n");
                g_recv_flag = 0;
                ChatRoomLogout(fd, username);
                return SUCCESS;
                break;
            default:
                printf("无效操作\n");
        }
    }

    return SUCCESS;
}

/* 退出登录 */
static int ChatRoomLogout(int fd, const char *username)
{
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("logout"));
    json_object_object_add(jobj, "name", json_object_new_string(username));
    const char *json = json_object_to_json_string(jobj);
    SendJsonToServer(fd, json);
    return SUCCESS;
}

/* 将未读消息写入本地文件 */
static int ChatRoomSaveUnreadMsg(json_object *json, const char *path)
{
    if(json != NULL)
    {
        int messages_num = json_object_array_length(json);
        for(int i = 0; i < messages_num; i++)
        {
            json_object *message = json_object_array_get_idx(json, i);
            /* 获取发送人 */
            json_object *sender_nameJson = json_object_object_get(message, "sender_name");
            /* 获取信息 */
            json_object *messageJson = json_object_object_get(message, "message");
            /* 获取时间 */
            json_object *timeJson = json_object_object_get(message, "send_time");
            if (sender_nameJson == NULL || messageJson == NULL || timeJson == NULL)
            {
                printf("接收消息失败, 接收到的消息不完整\n");
                continue;
            }
            const char *sender_name = json_object_get_string(sender_nameJson);
            const char *messageStr = json_object_get_string(messageJson);
            const char *time = json_object_get_string(timeJson);
            
            char privateChatRecordPath[PATH_SIZE] = {0};
            /* 可能存在的群名 */
            json_object *groupNameJson = json_object_object_get(message, "group_name");
            if(groupNameJson != NULL)
            {
                const char *groupName = json_object_get_string(groupNameJson);
                JoinPath(privateChatRecordPath, path, groupName);
            }
            else
            {                
                JoinPath(privateChatRecordPath, path, sender_name);
            }
            /* 打开私聊的本地聊天记录文件 */
            FILE *fp = fopen(privateChatRecordPath, "a+");
            if(fp == NULL)
            {
                printf("打开%s的文件失败\n",sender_name);
                continue;
            }
            /* 写入聊天记录 */
            fprintf(fp, "[%s] %s:\n%s\n", sender_name, time, messageStr);
            fclose(fp);
        }
    }
}