#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_ID_LEN 32
#define MAX_PAYLOAD_SIZE 1024

typedef enum {
    LTM_LOGIN = 1,
    LTM_REGISTER,
    LTM_JOIN_GRP,
    LTM_LEAVE_GRP,
    LTM_MESSAGE,
    LTM_HISTORY,
    LTM_FILE_META,
    LTM_FILE_CHUNK,
    LTM_DOWNLOAD,
    LTM_ERROR,
    LTM_GROUP_CMD,
    LTM_USERS_CMD,

    LTM_AUTH_REQ, 
    LTM_AUTH_RESP 
} PacketType;

// BẮT ĐẦU ÉP KIỂU 1 BYTE (Không padding)
#pragma pack(push, 1)

typedef struct {
    uint8_t type;               // 1 byte
    uint32_t payload_size;      // 4 bytes
    char target_id[MAX_ID_LEN]; // 32 bytes
    char sender_id[MAX_ID_LEN]; // 32 bytes
} PacketHeader; 
// Tổng cộng chính xác: 1 + 4 + 32 + 32 = 69 bytes

// KẾT THÚC ÉP KIỂU
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif