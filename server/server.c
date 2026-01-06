#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <process.h> 
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#include "../libs/common/protocol.h"
#include "../libs/common/net_utils.h"
#include "../libs/common/sqlite.h"

#include "topic_svc.h"
#include "history.h"
#include "file_svc.h"

#define DEFAULT_PORT 910
static volatile int g_running = 1;
static SOCKET g_listen_sock = INVALID_SOCKET;

static void parse_auth(const char *payload, char *mode, char *pw) {
    const char *sep = strchr(payload, '|');
    if (!sep) { strcpy(mode, payload); pw[0]=0; }
    else {
        size_t len = sep - payload;
        memcpy(mode, payload, len); mode[len] = 0;
        strcpy(pw, sep + 1);
    }
}

static void send_err(SOCKET s, const char *msg) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr)); // FIX: Memset
    hdr.type = LTM_ERROR;
    hdr.payload_size = (uint32_t)strlen(msg);
    strcpy(hdr.sender_id, "SERVER");
    
    send_all(s, &hdr, sizeof(hdr));
    if(hdr.payload_size) send_all(s, msg, (int)hdr.payload_size);
}

DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET s = *(SOCKET*)arg;
    free(arg);
    printf("[SERVER] Client connected (sock %d)\n", (int)s);

    PacketHeader hdr;
    char payload[MAX_PAYLOAD_SIZE + 1];

    while (g_running) {
        // RESET struct để tránh dùng lại dữ liệu cũ nếu recv lỗi nhẹ
        memset(&hdr, 0, sizeof(hdr));
        memset(payload, 0, sizeof(payload));

        // SỬA LỖI QUAN TRỌNG: Kiểm tra <= 0 thay vì < 0
        int bytes_read = recv_all(s, &hdr, sizeof(hdr));
        if (bytes_read <= 0) break; // Ngắt kết nối nếu lỗi hoặc client đóng (return 0)

        // Kiểm tra tính hợp lệ của gói tin
        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            printf("[SERVER] Payload too big (%u). Dropping client.\n", hdr.payload_size);
            break;
        }

        if (hdr.payload_size > 0) {
            if (recv_all(s, payload, (int)hdr.payload_size) <= 0) break; // Sửa < 0 thành <= 0
            payload[hdr.payload_size] = '\0';
        } else {
            payload[0] = '\0';
        }

        switch (hdr.type) {
            // ... (Giữ nguyên toàn bộ logic switch case cũ) ...
            // Chỉ cần đảm bảo các chỗ send_all khác không bị thay đổi logic
            case LTM_LOGIN: {
                char mode[16] = {0}, pw[64] = {0};
                parse_auth(payload, mode, pw);
                printf("[SERVER] LOGIN from %s, mode=%s\n", hdr.sender_id, mode);
                
                if (strcmp(mode, "CHECK") == 0) {
                    send_err(s, db_user_exists(hdr.sender_id) ? "USER_EXISTS" : "USER_NOT_EXISTS");
                } else if (strcmp(mode, "LOGIN") == 0) {
                    if (db_verify_user(hdr.sender_id, pw)) {
                        PacketHeader ack;
                        memset(&ack, 0, sizeof(ack));
                        ack.type = LTM_LOGIN;
                        strcpy(ack.target_id, hdr.sender_id);
                        strcpy(ack.sender_id, "SERVER");
                        send_all(s, &ack, sizeof(ack));

                        topic_persistence_load(s, hdr.sender_id);
                    } else {
                        send_err(s, "INVALID_PASSWORD");
                    }
                }
                break;
            }
            // ... (Các case khác giữ nguyên) ...
            case LTM_REGISTER: {
                // ... (Code cũ) ...
                if (db_create_user(hdr.sender_id, payload)) send_err(s, "REGISTER_OK");
                else send_err(s, "REGISTER_FAILED");
                // KHÔNG closesocket(s) ở đây để client tự xử lý hoặc giữ kết nối nếu muốn
                break;
            }
            case LTM_JOIN_GRP: {
                if (topic_exists(hdr.target_id)) {
                    printf("[SERVER] %s joined group %s\n", hdr.sender_id, hdr.target_id);
                    topic_subscribe(s, hdr.target_id);
                    topic_persistence_add(hdr.sender_id, hdr.target_id);
                    history_replay(s, hdr.target_id);
                } else send_err(s, "GROUP_NOT_FOUND");
                break;
            }
            case LTM_LEAVE_GRP:
                topic_unsubscribe(s, hdr.target_id);
                break;
            case LTM_GROUP_CMD:
                // ... (Giữ nguyên logic xử lý group cmd) ...
                if (strcmp(payload, "CREATE") == 0) {
                     if (!topic_exists(hdr.target_id)) {
                        topic_create(hdr.target_id);
                        // Gửi thông báo thành công (Code cũ của bạn)
                        // ...
                     }
                } else if (strcmp(payload, "LIST") == 0) {
                    topic_get_list(hdr.target_id, 100);
                }
                break;
            // case LTM_USERS_CMD:
            //     db_list_user();
            case LTM_MESSAGE:
                printf("[SERVER] MSG from %s to %s\n", hdr.sender_id, hdr.target_id);
                history_log(hdr.target_id, hdr.sender_id, "MSG", payload);
                // Logic lưu persistence
                if (strncmp(hdr.target_id, "user/", 5) == 0) {
                    topic_persistence_add(hdr.sender_id, hdr.target_id);
                    char receiver_topic[64]; 
                    snprintf(receiver_topic, 64, "user/%s", hdr.sender_id);
                    topic_persistence_add(hdr.target_id + 5, receiver_topic);
                } else {
                    topic_persistence_add(hdr.sender_id, hdr.target_id);
                }
                topic_route_msg(s, &hdr, payload);
                break;
            case LTM_FILE_META:
                file_handle_meta(s, &hdr, payload);
                break;
            case LTM_FILE_CHUNK:
                file_handle_chunk(s, &hdr, payload);
                break;
            case LTM_DOWNLOAD:
                file_handle_download(s, &hdr, payload);
                break;
        }
    }

    file_cancel_uploads(s);
    topic_remove_socket(s);
    closesocket(s);
    printf("[SERVER] Client disconnected (sock %d)\n", (int)s);
    return 0;
}

int main(int argc, char *argv[]) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    db_init("users.db");
    topic_svc_init();
    file_svc_init();

    g_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { AF_INET, htons(DEFAULT_PORT) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(g_listen_sock, 20);

    printf("[SERVER] Modular Server running on %d...\n", DEFAULT_PORT);
    printf("[DEBUG] Server PacketHeader size: %llu bytes (Must be 69)\n", sizeof(PacketHeader)); // Thêm dòng này
    while(g_running) {
        SOCKET client = accept(g_listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        SOCKET *arg = malloc(sizeof(SOCKET));
        *arg = client;
        CreateThread(NULL, 0, client_thread, arg, 0, NULL);
    }

    topic_svc_cleanup();
    file_svc_cleanup();
    db_close();
    WSACleanup();
    return 0;
}