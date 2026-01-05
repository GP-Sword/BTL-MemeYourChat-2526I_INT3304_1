#ifndef TOPIC_MGR_H
#define TOPIC_MGR_H

#include <winsock2.h>
#include "../libs/common/protocol.h"

void topic_svc_init(void);
void topic_svc_cleanup(void);

// Đăng ký user vào một topic (group/user)
void topic_subscribe(SOCKET sock, const char *topic_name);

// Rời topic
void topic_unsubscribe(SOCKET sock, const char *topic_name);

// Xóa user khỏi mọi topic (khi disconnect)
void topic_remove_socket(SOCKET sock);

// Gửi tin nhắn đến tất cả subscriber trong topic (trừ sender)
void topic_route_msg(SOCKET sender_sock, PacketHeader *hdr, const char *payload);

// Kiểm tra nhóm tồn tại
int topic_exists(const char *topic_name);

// Get hoặc Create topic
void topic_create(const char *topic_name);

// Lấy danh sách nhóm
int topic_get_list(char *buf, int max_len); 

// --- MỚI: PERSISTENCE (LƯU FILE TXT) ---
// Lưu thông tin user đã tham gia topic nào vào file
void topic_persistence_add(const char *username, const char *topic);

// Load lại danh sách topic của user khi login
void topic_persistence_load(SOCKET sock, const char *username);

#endif