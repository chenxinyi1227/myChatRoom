#ifndef __CHAT_ROOM_H__
#define __CHAT_ROOM_H__
#include <json-c/json_object.h>

/* 聊天室初始化 */
int ChatRoomInit();

/* 聊天室退出 */
int ChatRoomExit();

/* 聊天室注册 */
int ChatRoomRegister(int sockfd);

/* 聊天室登录 */
int ChatRoomLogin(int sockfd);

/* 添加好友 */
int ChatRoomAddFriend(int sockfd, const char *name, json_object *friends, const char *username);

/* 显示好友 */
int ChatRoomShowFriends(int sockfd, json_object* friends, const char *username, const char * path);

/* 删除好友 */
int ChatRoomDelFriend(int sockfd, const char *name, json_object *friends, const char *username);

/* 私聊 */
int ChatRoomPrivateChat(int sockfd, const char *name, json_object *friends, const char *username, const char * path);

/* 发起群聊 */
int ChatRoomAddGroupChat(int sockfd, const char *groupname, json_object *groups, const char *username);

/* 显示群聊列表 */
int ChatRoomShowGroupChat(int sockfd, json_object *groups, const char *username, const char *path);

/* 群聊 */
int ChatRoomGroupChat(int sockfd, const char *name, json_object *groups, const char *username, const char *path);

/* 退出群聊 */
int ChatRoomExitGroupChat(int sockfd, const char *groupname, const char *username);


#endif // __CHAT_ROOM_H__