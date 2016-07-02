
#ifndef MIRACLE_UIBCCTL_H
#define MIRACLE_UIBCCTL_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <netdb.h>
#include<arpa/inet.h>
#include <math.h>
#include "shl_log.h"

typedef enum {
        GENERIC_TOUCH_DOWN = 0,
        GENERIC_TOUCH_UP,
        GENERIC_TOUCH_MOVE,
        GENERIC_KEY_DOWN,
        GENERIC_KEY_UP,
        GENERIC_ZOOM,
        GENERIC_VERTICAL_SCROLL,
        GENERIC_HORIZONTAL_SCROLL,
        GENERIC_ROTATE
} MessageType;

typedef struct {
   char* m_PacketData;
   size_t m_PacketDataLen;
   bool m_DataValid;
} UibcMessage;

UibcMessage buildUibcMessage(MessageType type, const char* inEventDesc, double widthRatio, double heightRatio);

static char** str_split(char* pStr, const char* pDelim, size_t* size);

void getUIBCGenericTouchPacket(const char *inEventDesc, UibcMessage* uibcmessage, double widthRatio, double heightRatio);
void getUIBCGenericKeyPacket(const char *inEventDesc, UibcMessage* uibcmessage);
void getUIBCGenericZoomPacket(const char *inEventDesc,UibcMessage* uibcmessage);
void getUIBCGenericScalePacket(const char *inEventDesc, UibcMessage* uibcmessage);
void getUIBCGenericRotatePacket(const char *inEventDesc, UibcMessage* uibcmessage);

void hexdump(void *_data, size_t len);
void binarydump(void *_data, size_t len);

int sendUibcMessage(UibcMessage* uibcmessage, int sockfd);
#endif
