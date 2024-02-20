#include "threadPoll.h"
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
#include <mysql/mysql.h>
#include <fcntl.h>

#define SERVER_PORT 8888        // 服务器端口号,暂定为8888
#define SERVER_IP "172.18.188.222"   // 服务器ip,暂定为本机ip
#define NAME_SIZE 10            // 用户名长度
#define PASSWORD_SIZE 20        // 密码长度
#define MAX_FRIEND_NUM 10       // 最大好友数量
#define MAX_GROUP_NUM 10        // 最大群组数量
#define MAX_GROUP_MEMBERS_NUM 20// 最大群组成员数量
#define CONTENT_SIZE 1024       // 信息内容长度
#define PATH_SIZE 256           // 文件路径长度
#define MAX_SQL_LEN 1024        // sql语句长度
#define TIME_LEN 20             // 时间长度
#define MAX_LISENT_NUM 128      // 最大监听数
#define MAX_BUFFER_SIZE 1024    // 最大缓冲区大小
#define MIN_POLL_NUM 5          // 最小线程池数量
#define MAX_POLL_NUM 10          // 最大线程池数量
#define MAX_QUEUE_NUM 50      // 最大队列数量

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
    DATABASE_ERROR = -13,// 数据库错误
    SETSOCKOPT_ERROR = -14,// 设置socket选项错误

};

// 定义结构体来保存任务参数
typedef struct TaskArgs
{
    int client_fd;
    MYSQL *mysql;
} TaskArgs;

/* 接收请求并分类/处理请求 */
void *handleRequest(void* arg);
/* 用户注册 */
static int userRegister(int client_fd, json_object *json, MYSQL *mysql);
/* 用户登录 */
static int userLogin(int client_fd, json_object *json,  MYSQL *mysql);
/* 数据库查询 */
static int sqlQuery(const char *sql, MYSQL *mysql, MYSQL_RES **res);
/* 获取离线消息 */
static int getOfflineMessages(const char *name, json_object *json, MYSQL *mysql);
/* 获取用户的群组和好友列表 */
static int getUserInfo(const char *name, json_object *json,  MYSQL *mysql);
/* 更新用户在线状态 */
static int updateUserStatus(const char *name, int status, MYSQL *mysql);
/* 好友私聊 */
static int privateChat(int client_fd, json_object *json,  MYSQL *mysql);
/* 向好友发送消息 */
static int sendMessage(const char *name, const char *friendName, const char *message , MYSQL *mysql, json_object *returnJson);
/* 退出登录 */
static int userLogout(int client_fd, json_object *json,  MYSQL *mysql);
/* 获取当前时间 */
static char *getCurrentTime();
/* 添加好友 */
static int addFriend(int client_fd, json_object *json, MYSQL *mysql);
/* 删除好友 */
static int delFriend(int client_fd, json_object *json, MYSQL *mysql);
/* 添加群组 */
static int addGroup(int client_fd, json_object *json, MYSQL *mysql);
/* 群聊 */
static int groupChat(int client_fd, json_object *json, MYSQL *mysql);
/* 发送消息给群成员 */
static int sendMessageToGroup(const char *sendName, const char *groupName, const char *memberName, const char *message, MYSQL *mysql, json_object *returnJson);
/* 创建群组 */
static int createGroupChat(int client_fd, json_object *json, MYSQL *mysql);
//接收文件
static int fileUpload(int client_fd, json_object *json,  MYSQL *mysql);

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

/* 主函数 */
int main()
{   
    //创建文件传输缓存目录
    if(access("./fileDirectory", F_OK) == -1)
    {
        if (mkdir("./fileDirectory", 0777) == -1)
        {
            perror("mkdir error");
            return MALLOC_ERROR;
        }
    }

    /* 初始化线程池 */
    thread_poll_t poll;
    threadPollInit(&poll, MIN_POLL_NUM, MAX_POLL_NUM, MAX_QUEUE_NUM);

    /* 初始化服务 */ 
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket error");
        return SOCKET_ERROR;
    }
    // 启用端口复用
    int reuseaddr = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1)
    {
        perror("setsockopt error");
        close(server_fd);
        return SETSOCKOPT_ERROR;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    /* 绑定服务器信息 */
    int ret = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0)
    {
        perror("bind error");
        return CONNECT_ERROR;
    }

    /* 开始监听 */
    ret = listen(server_fd, 10);
    if (ret < 0)
    {
        perror("listen error");
        return CONNECT_ERROR;
    }
    printf("server start success\n");

    /* 初始化数据库 */
    MYSQL *mysql = mysql_init(NULL);
    if (mysql == NULL)
    {
        printf("mysql_init error:%s\n",mysql_error(mysql));
        return DATABASE_ERROR;
    }

    {
    /* 连接数据库 */
    mysql_real_connect(mysql, "localhost", "root", "1234", "test", 3306, NULL, 0);
    if (mysql == NULL)
    {
        printf("mysql_real_connect error:%s\n",mysql_error(mysql));
        return DATABASE_ERROR;
    }
    printf("database connect success\n");
    /* 设置字符集 */
    mysql_set_character_set(mysql, "utf8");
    /* 设置编码 */
    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8");
    
    /* 建表 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "create table if not exists users(id int primary key auto_increment, name varchar(%d), password varchar(%d))", NAME_SIZE, PASSWORD_SIZE);
    int sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("create users table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create users table success\n");
    /* 好友关系表 */
    sprintf(sql, "create table if not exists friends(id int primary key auto_increment, name varchar(%d), friend_name varchar(%d), messages_num int(2) not NULL)", NAME_SIZE,NAME_SIZE);
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {

        printf("create friends table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create friends table success\n");
    /* 群组表 */
    sprintf(sql, "create table if not exists chatgroups(id int primary key auto_increment, groupMainName varchar(%d), group_name varchar(%d))", NAME_SIZE, NAME_SIZE);  // groups 这个表名不能用
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {

        printf("create chatgroups table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create groups table success\n");
    /* 群组成员表 */
    sprintf(sql, "create table if not exists group_members(id int primary key auto_increment, group_name varchar(%d), member_name varchar(%d), messages_num int(2) not NULL)", NAME_SIZE, NAME_SIZE);
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("create group_members table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create group_members table success\n");
    /* 在线用户表 */
    sprintf(sql, "create table if not exists online_users(name varchar(%d), client_fd int primary key)", NAME_SIZE);
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("create online_users table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create online_users table success\n");
    /* 消息表 */
    sprintf(sql, "create table if not exists messages(id int primary key auto_increment, sender_name varchar(%d), receiver_name varchar(%d), message varchar(%d), send_time datetime)", NAME_SIZE, NAME_SIZE, CONTENT_SIZE);
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("create messages table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create messages table success\n");
    /* 群消息表 */
    sprintf(sql, "create table if not exists group_messages(id int primary key auto_increment, group_name varchar(%d), sender_name varchar(%d), receiver_name varchar(%d), message varchar(%d), send_time datetime)", NAME_SIZE, NAME_SIZE, NAME_SIZE, CONTENT_SIZE);
    if (mysql_query(mysql, sql) != 0)
    {
        printf("create group_messages table error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    printf("create group_messages table success\n");

    /* 添加触发器 */
    /* 自动增加未读消息数 */
    sprintf(sql, "create trigger if not exists after_message_insert after insert on messages for each row update friends set messages_num = messages_num + 1 where name = new.receiver_name and friend_name = new.sender_name");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_message_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    /* 上线后清除未读消息数 */
    sprintf(sql, "create trigger if not exists after_online_insert after insert on online_users for each row update friends set messages_num = 0 where name = new.name");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_online_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    /* 将系统(SYSTEM)添加为新注册账号的单向好友 */
    sprintf(sql, "create trigger if not exists after_register_insert after insert on users for each row insert into friends(name, friend_name, messages_num) values(new.name, 'SYSTEM', 0)");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_register_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    /* 建群时默认把群主加到群成员表 */
    sprintf(sql, "create trigger if not exists after_groupMain_insert after insert on chatgroups for each row insert into group_members(group_name, member_name, messages_num) values(new.group_name, new.groupMainName, 0)");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_group_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    /* 群消息未读时增加群成员对应的消息数 */
    sprintf(sql, "create trigger if not exists after_group_message_insert after insert on group_messages for each row update group_members set messages_num = messages_num + 1 where group_name = new.group_name and member_name = new.receiver_name");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_group_message_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    /* 上线后清除群未读消息数 */
    sprintf(sql, "create trigger if not exists after_online_group_insert after insert on online_users for each row update group_members set messages_num = 0 where member_name = new.name");
    if(mysql_query(mysql, sql) != 0)
    {
        printf("create after_online_group_insert trigger error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    

    }

    /* 创建套接字 */
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    /* 接收请求 */
    socklen_t client_addr_len = sizeof(client_addr);
    /* 启动服务 */
    while (1)
    {    
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            perror("accept error");
            continue;
        }
        printf("client connect success\n");
#if 0
        /* 处理请求 */
        handleRequest(client_fd,mysql);
#else 
        TaskArgs *args = (TaskArgs*)malloc(sizeof(TaskArgs));
        if (args == NULL)
        {
            perror("malloc error");
            continue;
        }
        args->client_fd = client_fd;
        args->mysql = mysql;
        /* 添加到任务队列 */
        threadPollAddTask(&poll, handleRequest, (void*)args);
#endif
        // break;
    }
    close(server_fd);
    return 0;
}

/* 处理请求 */
void *handleRequest(void* arg)
{
    /* 线程分离 */
    if (pthread_detach(pthread_self()) != 0)
    {
        perror("pthread_detach error");
        return NULL;
    }
    printf("handleRequest start\n");
    int client_fd = ((TaskArgs*)arg)->client_fd;
    MYSQL *mysql =  ((TaskArgs*)arg)->mysql;
    char recvJson[CONTENT_SIZE] = {0};
    while(1)
    {
        memset(recvJson, 0, sizeof(recvJson));
        /* 接收json字符串 */
        int ret = recv(client_fd, recvJson, CONTENT_SIZE, 0);
        if (ret == -1)
        {
            perror("recv error");
            return NULL;
        }
        if(ret == 0)//对方关闭连接
        {
            printf("client disconnect\n");
            /* 移除在线状态 */
            char sql[MAX_SQL_LEN] = {0};
            sprintf(sql, "delete from online_users where client_fd = %d", client_fd);
            int sql_ret = mysql_query(mysql, sql);
            if (sql_ret != 0)
            {
                printf("delete online_users error:%s\n", mysql_error(mysql));
                return NULL;
            }
            /* 释放参数 */
            if(arg != NULL)
            {
                free(arg);
                arg = NULL;
            }
            /* 释放线程 */
            close(client_fd);
            return NULL;
        }
        printf("recv json: %s\n", recvJson);
        /* 解析json */
        json_object *jobj = json_tokener_parse(recvJson);
        if (jobj == NULL)
        {
            printf("json parse error\n");
            return NULL;
        }
        /* 获取json中的type */
        json_object *type = json_object_object_get(jobj, "type");
        if (type == NULL)
        {
            printf("json type error\n");
            return NULL;
        }
        /* 获取json中的内容 */
        const char *typeStr = json_object_get_string(type);
        if (typeStr == NULL)
        {
            printf("json type error\n");
            return NULL;
        }
        /* 分类处理请求 */
        if(strcmp(typeStr, "register") == 0)
        {
            /* 注册 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            userRegister(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "login") == 0)
        {
            /* 登录 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            userLogin(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "logout") == 0)
        {
            /* 退出登录 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            userLogout(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "private") == 0)
        {
            /* 私聊 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            privateChat(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "addfriend")==0)
        {
            /* 加好友 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            addFriend(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "delfriend")==0)
        {
            /* 删好友 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            delFriend(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "groupchat") == 0)
        {
            /* 群聊 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            groupChat(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "createGroupChat") == 0)
        {
            /* 创建群组 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            createGroupChat(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "addGroup") == 0)
        {
            /* 添加群聊名称 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            addGroup(client_fd, jobj, mysql);
        }
        else if(strcmp(typeStr, "fileUpload") == 0)
        {
            /* 创建查看文件 */
            /* 消除没用的请求类型*/
            json_object_object_del(jobj, "type");
            fileUpload(client_fd, jobj, mysql);
        }
        else
        {
            /* 无效类型 */
            printf("invalid type\n");
        }
        // return NULL;


    }
    return NULL;

}

/* 注册 */
static int userRegister(int client_fd, json_object *json,  MYSQL *mysql)
{
    printf("注册\n");
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *password = json_object_get_string(json_object_object_get(json, "password"));
    printf("name: %s\n", name);
    printf("password: %s\n", password);

    /* 返回用json */
    json_object *returnJson = json_object_new_object();

    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select * from users where name='%s'", name);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    int sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        if (num_rows > 0)
        {
            json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
            json_object_object_add(returnJson, "reason", json_object_new_string("用户名已存在"));
        }
        else
        {
            /* 插入数据库 */
            sprintf(sql, "insert into users(name, password) values('%s', '%s')", name, password);
            sql_ret = mysql_query(mysql, sql);
            if (sql_ret != 0)
            {
                printf("sql insert error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
            }
            else
            {
                /* 注册成功 */
                printf("sql: %s\n", sql);
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
                json_object_object_add(returnJson, "name", json_object_new_string(name));
                json_object * friends = json_object_new_object();
                json_object_object_add(friends, "SYSTEM", json_object_new_int(0));
                json_object_object_add(returnJson, "friends",friends);
                json_object * groups = json_object_new_object();
                json_object_object_add(returnJson, "groups", groups);
                json_object * messages = json_object_new_array();
                json_object_object_add(returnJson, "messages", messages);
                /* 记录登录状态 */
                updateUserStatus(name, client_fd, mysql);
            }
        }
    }

    const char *sendJson = json_object_to_json_string(returnJson);
    printf("send json: %s\n", sendJson);
    /* 发送json */
    int ret = send(client_fd, sendJson, strlen(sendJson), 0);

    /* 释放json */
    json_object_put(json);
    json_object_put(returnJson);

    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }

    if (ret == -1)
    {
        perror("send error");
        return SEND_ERROR;
    }
    return SUCCESS;
}

/* 登录 */
static int userLogin(int client_fd, json_object *json,  MYSQL *mysql)
{
    printf("登录\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();
    /* 获取json中的内容 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *password = json_object_get_string(json_object_object_get(json, "password"));
    printf("name: %s\n", name);
    printf("password: %s\n", password);
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select password from users where name='%s'", name);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    int sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        if (num_rows > 0)
        {
            /* 判断密码是否正确 */
            MYSQL_ROW row = mysql_fetch_row(res);
            const char *dbPassword = row[0];
            if (strcmp(password, dbPassword) == 0)
            {
                /* 登录成功 */
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
                json_object_object_add(returnJson, "name", json_object_new_string(name));
                getUserInfo(name, returnJson, mysql);
                getOfflineMessages(name, returnJson, mysql);
                /* 记录登录状态 */
                updateUserStatus(name, client_fd, mysql);
            }
            else
            {
                /* 密码错误 */
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("密码错误"));
            }
        }
        else
        {
            /* 用户名不存在 */
            json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
            json_object_object_add(returnJson, "reason", json_object_new_string("用户名不存在"));
        }
    }
    /* 发送json */
    const char *sendJson = json_object_to_json_string(returnJson);
    printf("send json: %s\n", sendJson);
    int ret = send(client_fd, sendJson, strlen(sendJson), 0);
    /* 释放json */
    json_object_put(json);
    json_object_put(returnJson);
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    if (ret == -1)
    {
        perror("send error");
        return SEND_ERROR;
    }
    return SUCCESS;
}

/* 获取离线消息 */
static int getOfflineMessages(const char *name, json_object *json, MYSQL *mysql)
{
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select sender_name, message, send_time from messages where receiver_name='%s'", name);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    MYSQL_ROW row;
    int num_rows = mysql_num_rows(res);     // 行数
    int idx = 0;
    json_object *friend_messages = json_object_new_array();
    while ((row = mysql_fetch_row(res)))
    {
        json_object *message = json_object_new_object();
        json_object_object_add(message, "sender_name", json_object_new_string(row[0]));
        json_object_object_add(message, "message", json_object_new_string(row[1]));
        json_object_object_add(message, "send_time", json_object_new_string(row[2]));
        json_object_array_add(friend_messages, message);
        idx++;
        if (idx == num_rows)
        {
            break;
        }
    }
    json_object_object_add(json, "frinend_messages", friend_messages);
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 删除已读消息 */
    sprintf(sql, "delete from messages where receiver_name='%s'", name);
    printf("sql: %s\n", sql);
    sqlQuery(sql, mysql, &res);
    memset(sql, 0, sizeof(sql));
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 未读群消息 */
    sprintf(sql, "select sender_name,group_name, message, send_time from group_messages where receiver_name='%s'", name);
    printf("sql: %s\n", sql);
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    memset(sql, 0, sizeof(sql));
    num_rows = mysql_num_rows(res);     // 行数
    idx = 0;
    json_object *group_messages = json_object_new_array();
    while ((row = mysql_fetch_row(res)))
    {
        json_object *message = json_object_new_object();
        json_object_object_add(message, "sender_name", json_object_new_string(row[0]));
        json_object_object_add(message, "group_name", json_object_new_string(row[1]));
        json_object_object_add(message, "message", json_object_new_string(row[2]));
        json_object_object_add(message, "send_time", json_object_new_string(row[3]));
        json_object_array_add(group_messages, message);
        idx++;
        if (idx == num_rows)
        {
            break;
        }
    }
    json_object_object_add(json, "group_messages", group_messages);
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 删除已读消息 */
    sprintf(sql, "delete from group_messages where receiver_name='%s'", name);
    printf("sql: %s\n", sql);
    sqlQuery(sql, mysql, &res);

    memset(sql, 0, sizeof(sql));
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    return SUCCESS;
}

/* 获取用户的群组和好友列表 */
static int getUserInfo(const char *name, json_object *json,  MYSQL *mysql)
{
    MYSQL_RES *res = NULL;
    char sql[MAX_SQL_LEN] = {0};
    /* 查好友列表 */
    sprintf(sql, "select friend_name,messages_num from friends where name='%s'", name);
    int sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    MYSQL_ROW row;
    int num_rows = mysql_num_rows(res);     // 行数
    int i = 0;
    json_object *friends = json_object_new_object();
    while ((row = mysql_fetch_row(res)))
    {
        json_object_object_add(friends, row[0], json_object_new_int(atoi(row[1])));
        i++;
        if (i == num_rows)
        {
            break;
        }
    }
    json_object_object_add(json, "friends", friends);
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 查群组列表 */
    sprintf(sql, "select group_name,messages_num from group_members where member_name='%s'", name);
    sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    num_rows = mysql_num_rows(res);     // 行数
    i = 0;
    json_object *groups = json_object_new_object();
    while ((row = mysql_fetch_row(res)))
    {
        json_object_object_add(groups, row[0], json_object_new_int(atoi(row[1])));
        i++;
        if (i == num_rows)
        {
            break;
        }
    }
    json_object_object_add(json, "groups", groups);
        /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
}

/* 数据库查询 */
static int sqlQuery(const char *sql, MYSQL *mysql, MYSQL_RES **res)
{
    int sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("sql: %s\n", sql);
        printf("sql_ret:%d\n", sql_ret);
        printf("sqlQuery error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    *res = mysql_store_result(mysql);
    if (*res == NULL)
    {
        printf("sql store result error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    return SUCCESS;
}

/* 更新用户在线状态 */
static int updateUserStatus(const char *name, int status, MYSQL *mysql)
{
    char sql[MAX_SQL_LEN] = {0};
    MYSQL_RES *res = NULL;
    
    /* 查询数据库 */
    sprintf(sql, "select * from online_users where name='%s'", name);
    printf("sql: %s\n", sql);
    int sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    /* 处理数据库查询结果 */
    int num_rows = mysql_num_rows(res);     // 行数
    if (num_rows > 0)
    {
        /* 更新数据库 */
        sprintf(sql, "update online_users set client_fd=%d where name='%s'", status, name);
        /* todo... */
        /* 同时在线冲突 */
    }
    else
    {
        /* 插入数据库 */
        sprintf(sql, "insert into online_users(name, client_fd) values('%s', %d)", name, status);
    }
    sql_ret = mysql_query(mysql, sql);
    if (sql_ret != 0)
    {
        printf("sql insert error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    printf("sql: %s\n", sql);
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    return SUCCESS;

}

/* 好友私聊 */
static int privateChat(int client_fd, json_object *json,  MYSQL *mysql)
{
    printf("私聊\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();

    int ret = 0;

    /* 获取json中的内容 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *friendName = json_object_get_string(json_object_object_get(json, "friendName"));
    const char *message = json_object_get_string(json_object_object_get(json, "message"));
    printf("name: %s\n", name);
    printf("friendName: %s\n", friendName);
    printf("message: %s\n", message);

    /* 判断是否为双向好友 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select * from friends where name='%s' and friend_name='%s'", friendName, name);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    memset(sql, 0, sizeof(sql));
    int num_rows = mysql_num_rows(res);     // 行数
    if (num_rows == 0)
    {
        /* 不是双向好友 */
        json_object_object_add(returnJson, "type", json_object_new_string("private"));
        json_object_object_add(returnJson, "name", json_object_new_string(friendName));
        json_object_object_add(returnJson, "message", json_object_new_string("还未与对方成为好友,无法私聊"));
        json_object_object_add(returnJson, "time",json_object_new_string(getCurrentTime()));
    }
    else
    {
        /* 向好友发送消息 */
        sendMessage(name, friendName, message, mysql, returnJson);
    }

    /* 发送json */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    ret = send(client_fd, returnJsonStr, strlen(returnJsonStr), 0);
    if(ret < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }
    /* 释放json */
    json_object_put(json);
    json_object_put(returnJson);
    
    return SUCCESS;

}

/* 向好友发送消息 */
static int sendMessage(const char *name, const char *friendName, const char *message , MYSQL *mysql, json_object *returnJson)
{
    int ret = 0;
    
    /* 转发用json */
    json_object *forwardJson = json_object_new_object();
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select client_fd from online_users where name='%s'", friendName);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    int sql_ret = sqlQuery(sql, mysql, &res);
    memset(sql, 0, sizeof(sql));
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
        const char *returnJsonStr = json_object_to_json_string(returnJson);
        printf("send json: %s\n", returnJsonStr);
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        printf("num_rows: %d\n", num_rows);
        if (num_rows > 0)
        {
            /* 发送消息 */
            MYSQL_ROW row = mysql_fetch_row(res);
            int friend_fd = atoi(row[0]);
            printf("friend_fd: %d\n", friend_fd);
            json_object_object_add(forwardJson, "type", json_object_new_string("private"));
            json_object_object_add(forwardJson, "name", json_object_new_string(name));
            json_object_object_add(forwardJson, "message", json_object_new_string(message));
            json_object_object_add(forwardJson, "time", json_object_new_string(getCurrentTime()));
            const char *forwardJsonStr = json_object_to_json_string(forwardJson);
            printf("send json: %s\n", forwardJsonStr);
            ret = send(friend_fd, forwardJsonStr, strlen(forwardJsonStr), 0);
            if (ret == -1)
            {
                perror("send error");
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("发送失败"));
            }
            else
            {
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
            }
            
        }
        else if (num_rows == 0)
        {
            /* 对方未在线 */
            /* 好友列表里对应的好友关系的消息数+1 */
            sprintf(sql, "insert into messages(sender_name, receiver_name, message, send_time) values('%s', '%s', '%s', '%s');",
             name, friendName, message, getCurrentTime());
            sql_ret = mysql_query(mysql, sql);
            printf("sql: %s\n", sql);
            memset(sql, 0, sizeof(sql));
            if (sql_ret != 0)
            {
                printf("sql insert error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
            }
            json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
            json_object_object_add(returnJson, "reason", json_object_new_string("对方未在线"));
        }
    }
    
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    return SUCCESS;
}


/* 退出登录 */
static int userLogout(int client_fd, json_object *json,  MYSQL *mysql)
{
    char sql[MAX_SQL_LEN] = {0};
    MYSQL_RES *res = NULL;
    /* 查询数据库 */
    sprintf(sql, "select * from online_users where client_fd=%d", client_fd);
    printf("sql: %s\n", sql);
    int sql_ret = sqlQuery(sql, mysql, &res);
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        return DATABASE_ERROR;
    }
    /* 处理数据库查询结果 */
    int num_rows = mysql_num_rows(res);     // 行数
    if (num_rows > 0)
    {
        /* 更新数据库 */
        sprintf(sql, "delete from online_users where client_fd=%d", client_fd);
        sql_ret = mysql_query(mysql, sql);
        if (sql_ret != 0)
        {
            printf("sql insert error:%s\n", mysql_error(mysql));
            return DATABASE_ERROR;
        }
    }
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    return SUCCESS;
}
/* 获取当前时间 */
static char *getCurrentTime()
{
    time_t now;
    struct tm *tm;
    static char time_str[TIME_LEN] = {0};
    time(&now);
    tm = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
    return time_str;
}

/* 添加好友 */
static int addFriend(int client_fd, json_object *json, MYSQL *mysql)
{
    /* 添加好友的流程(A加B为好友)：
        A将申请发给系统
        系统判断B是否存在
            存在:
                系统将申请转发给B
                    send_name:系统
                    receive_name:B
                    message:A申请加为好友
                系统回复A成功
            不存在:
                系统回复A失败,并提示B不存在
        等待B回复
            好友申请:即成功,创建双向好友
            删除好友：即失败,向A发送失败说明

    */
    printf("添加好友\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();
    /* 转发用json */
    json_object *forwardJson = json_object_new_object();
    int ret = 0;
    /* 获取json中的内容 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *friendName = json_object_get_string(json_object_object_get(json, "friend"));
    printf("name: %s\n", name);
    printf("friendName: %s\n", friendName);
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select * from users where name='%s'", friendName);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        printf("num_rows: %d\n", num_rows);
        if (num_rows > 0)
        {
            /* 存在用户 */
            /* 添加单向好友关系 */
            memset(sql, 0, sizeof(sql));
            /* 判断单向好友关系是否存在 */
            sprintf(sql, "select * from friends where name='%s' and friend_name='%s'", name, friendName);
            printf("sql: %s\n", sql);
            if (sqlQuery(sql, mysql, &res) != 0)
            {
                /* 释放结果集 */
                if (res != NULL)
                {
                    mysql_free_result(res);
                    res = NULL;
                }
                return SUCCESS;
            }
            memset(sql, 0, sizeof(sql));
            sprintf(sql, "insert into friends(name, friend_name, messages_num) values('%s', '%s', 0)", name, friendName);
            printf("sql: %s\n", sql);
            if (mysql_query(mysql, sql) != 0)
            {
                printf("sql insert error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
            }
            else
            {
                /* 判断是否构成双向好友 */
                /* 查询数据库 */
                memset(sql, 0, sizeof(sql));
                sprintf(sql, "select * from friends where name='%s' and friend_name='%s'", friendName, name);
                printf("sql: %s\n", sql);
                if (sqlQuery(sql, mysql, &res) != 0)
                {
                    printf("sql query error:%s\n", mysql_error(mysql));
                    json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                    json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
                }
                else
                {
                    /* 处理数据库查询结果 */
                    int num_rows = mysql_num_rows(res);     // 行数
                    printf("num_rows: %d\n", num_rows);
                    if (num_rows > 0)
                    {
                        /* 构成双向好友 */
                        json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
                        json_object_object_add(returnJson, "reason", json_object_new_string("添加成功"));
                    }     {
                        /* 不构成双向好友 */
                        /* 转发消息 */
                        char message[CONTENT_SIZE] = {0};
                        sprintf(message, "%s申请加为好友", name);
                        sendMessage("SYSTEM", friendName, message, mysql, returnJson);
                        json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
                        json_object_object_add(returnJson, "reason", json_object_new_string("已发出申请"));
                    }
                }
            }
        }
    }
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 发送json */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    ret = send(client_fd, returnJsonStr, strlen(returnJsonStr), 0);
    if(ret < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }

    return SUCCESS;
}

/* 添加群组名 */
static int addGroup(int client_fd, json_object *json, MYSQL *mysql)
{
    printf("添加群名\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();
    /* 转发用json */
    json_object *forwardJson = json_object_new_object();
    int ret = 0;
    /* 获取json中的内容 */
    const char *groupname = json_object_get_string(json_object_object_get(json, "groupname"));
    const char *username = json_object_get_string(json_object_object_get(json, "name"));
    printf("groupname: %s\n", groupname);
    printf("username: %s\n", username);
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select * from chatgroups where groupMainName='%s'", groupname);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        printf("num_rows: %d\n", num_rows);
        if (num_rows == 0)
        {
            /* 不存在用户 */
            memset(sql, 0, sizeof(sql));
            sprintf(sql, "insert into group_members(group_name, member_name, messages_num) values('%s', '%s', 0)", groupname, username);
            printf("sql: %s\n", sql);
            if (mysql_query(mysql, sql) != 0)
            {
                printf("sql insert error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
            }
        }
    }
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 发送json */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    ret = send(client_fd, returnJsonStr, strlen(returnJsonStr), 0);
    if(ret < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }

    return SUCCESS;
}

/* 删除好友 */
static int delFriend(int client_fd, json_object *json, MYSQL *mysql)
{
    /* 删除流程:
        单向删除好友关系
    */
    printf("删除好友\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();
    /* 转发用json */
    json_object *forwardJson = json_object_new_object();

    /* 获取json中的内容 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *friendName = json_object_get_string(json_object_object_get(json, "friend"));
    printf("name: %s\n", name);
    printf("friendName: %s\n", friendName);
    /* 删除好友关系 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "delete from friends where name='%s' and friend_name='%s'", name, friendName);
    printf("sql: %s\n", sql);
    if (mysql_query(mysql, sql) != 0)
    {
        printf("sql insert error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
    }
    else
    {
        /* 转发消息 */
        char message[CONTENT_SIZE] = {0};
        sprintf(message, "%s已删除您为好友", name);
        sendMessage("SYSTEM", friendName, message, mysql, returnJson);
        json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
        json_object_object_add(returnJson, "reason", json_object_new_string("删除成功"));
    }
    /* 释放结果集 */
    MYSQL_RES *res = NULL;
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 发送json */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    if(send(client_fd, returnJsonStr, strlen(returnJsonStr), 0) < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }
    return SUCCESS;
}

/* 群聊 */
static int groupChat(int client_fd, json_object *json, MYSQL *mysql)
{
    /*  接到的消息
            name:发信人;
            groupName:群名称;
            message:消息内容;
        获取群成员,向所有成员转发消息
    */
    printf("群聊\n");
    /* 返回用json */
    json_object *returnJson = json_object_new_object();

    /* 获取json中的内容 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *groupName = json_object_get_string(json_object_object_get(json, "groupname"));
    const char *message = json_object_get_string(json_object_object_get(json, "message"));
    printf("name: %s\n", name);
    printf("groupName: %s\n", groupName);
    printf("message: %s\n", message);
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select member_name from group_members where group_name='%s' and member_name!='%s'", groupName,name);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    memset(sql, 0, sizeof(sql));
    int num_rows = mysql_num_rows(res);     // 行数
    printf("num_rows: %d\n", num_rows);
    if (num_rows > 0)
    {
        /* 群成员存在 */
        /* 转发消息 */
        MYSQL_ROW row = NULL;
        int idx = 0;
        char memberName[NAME_SIZE] = {0};
        while ((row = mysql_fetch_row(res)))
        {
            strcpy(memberName, row[0]);
            printf("memberName: %s\n", memberName);
            /* 转发消息 */
            sendMessageToGroup(name, groupName, memberName, message, mysql, returnJson);
            if(++idx >= num_rows)
            {
                break;
            }
        }
    }
    else
    {
        /* 群成员不存在 */
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("群成员不存在"));
    }

    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 发送回执 */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    if(send(client_fd, returnJsonStr, strlen(returnJsonStr), 0) < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }
    return SUCCESS;
}

/* 发送消息给群成员 */
static int sendMessageToGroup(const char *sendName, const char *groupName, const char *memberName, const char *message, MYSQL *mysql, json_object *returnJson)
{
    /* 转发用json */
    json_object *forwardJson = json_object_new_object();
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select client_fd from online_users where name='%s'", memberName);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    int sql_ret = sqlQuery(sql, mysql, &res);
    memset(sql, 0, sizeof(sql));
    if (sql_ret != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        /* 处理数据库查询结果 */
        int num_rows = mysql_num_rows(res);     // 行数
        printf("num_rows: %d\n", num_rows);
        if (num_rows > 0)
        {
            /* 成员在线 */
            /* 转发消息 */
            MYSQL_ROW row = mysql_fetch_row(res);
            int member_fd = atoi(row[0]);
            printf("client_fd: %d\n", member_fd);
            json_object_object_add(forwardJson, "type", json_object_new_string("groupchat"));
            json_object_object_add(forwardJson, "name", json_object_new_string(sendName));
            json_object_object_add(forwardJson, "message", json_object_new_string(message));
            json_object_object_add(forwardJson, "groupname", json_object_new_string(groupName));
            json_object_object_add(forwardJson, "time", json_object_new_string(getCurrentTime()));
            const char *forwardJsonStr = json_object_to_json_string(forwardJson);
            printf("send json: %s\n", forwardJsonStr);
            if(send(member_fd, forwardJsonStr, strlen(forwardJsonStr), 0) == -1)
            {
                perror("send error");
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("发送失败"));
            }
            else
            {
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
            }
        }
        else
        {
            /* 成员不在线 */
            /* 更新数据库 */
            sprintf(sql, "insert into group_messages(group_name, sender_name, receiver_name, message, send_time) values('%s', '%s', '%s', '%s', '%s')",
                    groupName, sendName, memberName, message, getCurrentTime());
            printf("sql: %s\n", sql);
            if (mysql_query(mysql, sql) != 0)
            {
                printf("sql update error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库更新错误"));
            }
            else
            {
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
                json_object_object_add(returnJson, "reason", json_object_new_string("对方未在线"));
            }
        }
        
    }

    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    return SUCCESS;
}

/* 创建群组 */
static int createGroupChat(int client_fd, json_object *json, MYSQL *mysql)
{
    printf("创建群组\n");
    /* 获取创建信息 */
    const char *name = json_object_get_string(json_object_object_get(json, "name"));
    const char *groupname = json_object_get_string(json_object_object_get(json, "groupname"));
    printf("name: %s\n", name);
    printf("groupName: %s\n", groupname);
    /* 返回用json */
    json_object *returnJson = json_object_new_object();
    json_object_object_add(returnJson, "type", json_object_new_string("createGroupChat"));
    /* 查询数据库 */
    char sql[MAX_SQL_LEN] = {0};
    sprintf(sql, "select * from chatgroups where group_name='%s'", groupname);
    printf("sql: %s\n", sql);
    MYSQL_RES *res = NULL;
    if (sqlQuery(sql, mysql, &res) != 0)
    {
        printf("sql query error:%s\n", mysql_error(mysql));
        json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
        json_object_object_add(returnJson, "reason", json_object_new_string("数据库查询错误"));
    }
    else
    {
        memset(sql, 0, sizeof(sql));
        int num_rows = mysql_num_rows(res);     // 行数
        printf("num_rows: %d\n", num_rows);
        if (num_rows > 0)
        {
            /* 群组存在 */
            json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
            json_object_object_add(returnJson, "reason", json_object_new_string("群组已存在"));
        }
        else
        {
            /* 群组不存在 */
            /* 插入数据库 */
            sprintf(sql, "insert into chatgroups(group_name, groupMainName) values('%s', '%s')", groupname, name);
            printf("sql: %s\n", sql);
            if (mysql_query(mysql, sql) != 0)
            {
                printf("\\\n\n");
                printf("sql insert error:%s\n", mysql_error(mysql));
                json_object_object_add(returnJson, "receipt", json_object_new_string("fail"));
                json_object_object_add(returnJson, "reason", json_object_new_string("数据库插入错误"));
            }
            else
            {
                /* 插入成功 */
                json_object_object_add(returnJson, "receipt", json_object_new_string("success"));
            }
        }
    }
    /* 释放结果集 */
    if (res != NULL)
    {
        mysql_free_result(res);
        res = NULL;
    }
    /* 发送json */
    const char *returnJsonStr = json_object_to_json_string(returnJson);
    printf("send json: %s\n", returnJsonStr);
    if(send(client_fd, returnJsonStr, strlen(returnJsonStr), 0) < 0)
    {
        perror("send error");
        return SEND_ERROR;
    }
    return SUCCESS;
}


//将接收到的文件写缓存目录下
static int receiveFile(int client_fd, int file_fd) 
{
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_received;
    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) 
    {
        ssize_t bytes_written = write(file_fd, buffer, bytes_received);
        if (bytes_written < 0) 
        {
            perror("写入文件内容失败");
            exit(EXIT_FAILURE);
        }
        // 清空buffer
        memset(buffer, 0, sizeof(buffer));
    }

    if (bytes_received < 0) 
    {
        perror("接收文件内容失败");
        exit(EXIT_FAILURE);
    }
    return SUCCESS;
    // printf("服务器文件暂存完成\n");
}

//接收文件
static int fileUpload(int client_fd, json_object *json,  MYSQL *mysql)
{
    char buffer[MAX_BUFFER_SIZE];
    // 读取文件名
    memset(buffer, 0, sizeof(buffer));
    if (recv(client_fd, buffer, sizeof(buffer), 0) <= 0) 
    {
        perror("读取文件名失败");
        exit(EXIT_FAILURE);
    }
    printf("接收文件：%s\n", buffer);

    // 打开文件
    char src_path[1024] = {0};
    char dir[1024] = {"./fileDirectory"};//文件缓存目录
    JoinPath(src_path, dir, buffer);
    printf("文件路径：%s\n", src_path);
    int file_fd = open(src_path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (file_fd < 0) 
    {
        perror("创建文件失败");
        return ILLEGAL_ACCESS;
    }
    int ret = receiveFile(client_fd, file_fd);
    if(ret == 0)
    {
        printf("服务器文件暂存完成\n");
    }
    else
    {
        printf("服务器文件暂存失败\n");
    }
}



