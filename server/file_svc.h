#ifndef FILE_SVC_H
#define FILE_SVC_H

#include <winsock2.h>
#include "../libs/protocol.h"

void file_svc_init(void);
void file_svc_cleanup(void);

// Xử lý gói META: tạo file, lưu thông tin vào list upload
void file_handle_meta(SOCKET sock, PacketHeader *hdr, const char *payload);

// Xử lý gói CHUNK: ghi vào file, forward cho người khác
void file_handle_chunk(SOCKET sock, PacketHeader *hdr, const char *payload);

// Hủy các upload đang dở dang của user khi disconnect
void file_cancel_uploads(SOCKET sock);

#endif