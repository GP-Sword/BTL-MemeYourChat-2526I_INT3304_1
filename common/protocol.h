#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_ID_LEN 32
#define MAX_PAYLOAD_SIZE 1024

typedef enum {
    LTM_LOGIN = 1,
    LTM_JOIN_GRP,
    LTM_LEAVE_GRP,
    LTM_MESSAGE,
    LTM_HISTORY,
    LTM_FILE_META,
    LTM_FILE_CHUNK,
    LTM_DOWNLOAD,
    LTM_ERROR
} PacketType;

#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint32_t payload_size;
    char target_id[MAX_ID_LEN]; // user/ hoặc group chat/
    char sender_id[MAX_ID_LEN]; // user id người gửi
} PacketHeader;

#pragma pack(pop)
#endif