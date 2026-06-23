#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <string.h>

//MODE
#define MODE_W1 1   // 단일 모델 처리
#define MODE_W2 2   // 단일 모델 처리
#define MODE_W3 3   // 단일 모델 처리

#define MODE_SERIAL   4  // 복수모델 직렬 처리
#define MODE_PARALLEL 5  // 복수모델 병렬 처리

//TYPE
#define TYPE_PROMPT 0
#define TYPE_W1 1
#define TYPE_W2 2
#define TYPE_W3 3
#define TYPE_SERV 4

//SERVER PORT
#define SERV_PORT 9909

typedef struct msg_frame
{
    int type;
    int mode;
    char name[16];
    int fd;
    int data_len;
    char data[1428];
} MsgFrame;
