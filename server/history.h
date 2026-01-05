#ifndef HISTORY_H
#define HISTORY_H

#include <winsock2.h>

// Ghi log 1 dòng vào file history.log của topic
void history_log(const char *topic, const char *sender, const char *kind, const char *content);

// Đọc file log và gửi lại cho client vừa join
void history_replay(SOCKET client_sock, const char *topic);

// Helper để tạo đường dẫn lưu file upload (dùng chung với module File)
void history_make_paths(const char *topic, char *group_name, char *files_dir);

#endif