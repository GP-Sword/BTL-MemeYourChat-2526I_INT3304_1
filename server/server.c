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
    PacketHeader hdr = { LTM_ERROR, (uint32_t)strlen(msg), "", "SERVER" };
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
        if (recv_all(s, &hdr, sizeof(hdr)) < 0) break;
        if (hdr.payload_size > MAX_PAYLOAD_SIZE) break;

        if (hdr.payload_size > 0) {
            if (recv_all(s, payload, (int)hdr.payload_size) < 0) break;
            payload[hdr.payload_size] = '\0';
        } else {
            payload[0] = '\0';
        }

        switch (hdr.type) {
            case LTM_LOGIN: {
                char mode[16] = {0}, pw[64] = {0};
                parse_auth(payload, mode, pw);
                printf("[SERVER] LOGIN from %s, mode=%s\n", hdr.sender_id, mode);
                
                if (strcmp(mode, "CHECK") == 0) {
                    send_err(s, db_user_exists(hdr.sender_id) ? "USER_EXISTS" : "USER_NOT_EXISTS");
                } else if (strcmp(mode, "LOGIN") == 0) {
                    if (db_verify_user(hdr.sender_id, pw)) {
                        // Login OK
                        PacketHeader ack = { LTM_LOGIN, 0, "", "SERVER" };
                        strcpy(ack.target_id, hdr.sender_id);
                        send_all(s, &ack, sizeof(ack));

                        // Auto-subscribe
                        char user_topic[64];
                        snprintf(user_topic, sizeof(user_topic), "user/%s", hdr.sender_id);
                        topic_subscribe(s, user_topic);
                        topic_subscribe(s, "group/global");
                        
                        history_replay(s, "group/global");
                    } else {
                        send_err(s, "INVALID_PASSWORD");
                    }
                }
                break;
            }
            case LTM_REGISTER: {
                printf("[SERVER] REGISTER request for user: %s\n", hdr.sender_id);
                if (db_create_user(hdr.sender_id, payload)) {
                    printf("[SERVER] Created new user: %s\n", hdr.sender_id);
                    send_err(s, "REGISTER_OK");
                }
                else {
                    printf("[SERVER] Register failed for %s\n", hdr.sender_id);
                    send_err(s, "REGISTER_FAILED");
                } 
                closesocket(s);
                return 0;
            }
            case LTM_JOIN_GRP: {
                if (topic_exists(hdr.target_id)) {
                    printf("[SERVER] %s joined group %s\n", hdr.sender_id, hdr.target_id);
                    topic_subscribe(s, hdr.target_id);
                    history_replay(s, hdr.target_id);
                    
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Joined %s. Use '/group leave %s' to exit.", hdr.target_id, hdr.target_id);
                } else {
                    send_err(s, "GROUP_NOT_FOUND. Use '/group create <name>' first.");
                }
                break;
            }
            case LTM_LEAVE_GRP:
                printf("[SERVER] %s left group %s\n", hdr.sender_id, hdr.target_id);
                topic_unsubscribe(s, hdr.target_id);
                break;
            case LTM_GROUP_CMD: {
                if (strcmp(payload, "CREATE") == 0) {
                    if (topic_exists(hdr.target_id)) {
                        send_err(s, "GROUP_ALREADY_EXISTS");
                    } else {
                        topic_create(hdr.target_id);
<<<<<<< HEAD
                        // Gửi thông báo thành công (Code cũ của bạn)
                        // ...
                     }
                } else if (strcmp(payload, "LIST") == 0) {
                    topic_get_list(hdr.target_id, 100);
                }
                break;
            // case LTM_USERS_CMD:
            //     db_list_user();
=======
                        printf("[SERVER] Created group: %s\n", hdr.target_id);
                        
                        PacketHeader notif = { LTM_MESSAGE, 0, "", "SERVER" };
                        strcpy(notif.target_id, "user");
                        strcpy(notif.target_id, hdr.sender_id); 
                        
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Group '%s' created successfully.", hdr.target_id);
                        notif.payload_size = strlen(msg);
                        send_all(s, &notif, sizeof(notif));
                        send_all(s, msg, notif.payload_size);
                    }
                } 
                else if (strcmp(payload, "LIST") == 0) {
                    char list_buf[1024];
                    topic_get_list(list_buf, sizeof(list_buf));
                    
                    PacketHeader notif = { LTM_MESSAGE, (uint32_t)strlen(list_buf), "", "SERVER" };
                    strcpy(notif.target_id, hdr.sender_id);
                    send_all(s, &notif, sizeof(notif));
                    send_all(s, list_buf, (int)notif.payload_size);
                }
                break;
            }
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
            case LTM_MESSAGE:
                printf("[SERVER] MSG from %s to %s: %s\n", hdr.sender_id, hdr.target_id, payload);
                history_log(hdr.target_id, hdr.sender_id, "MSG", payload);
                topic_route_msg(s, &hdr, payload);
                break;
            case LTM_FILE_META:
                file_handle_meta(s, &hdr, payload);
                break;
            case LTM_FILE_CHUNK:
                file_handle_chunk(s, &hdr, payload);
                break;
            case LTM_DOWNLOAD:
                // hdr.target_id phải là nơi chứa file đó (client phải gửi đúng)
                printf("[SERVER] Download request from %s for %s in %s\n", 
                       hdr.sender_id, payload, hdr.target_id);
                file_handle_download(s, &hdr, payload);
                break;
        }
    }

    // Cleanup
    file_cancel_uploads(s);
    topic_remove_socket(s);
    closesocket(s);
    printf("[SERVER] Client disconnected (sock %d)\n", (int)s);
    return 0;
}

int main(int argc, char *argv[]) {
    // Init Winsock & DB
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    db_init("users.db");

    // Init Modules
    topic_svc_init();
    file_svc_init();

    // Socket Setup
    g_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { AF_INET, htons(DEFAULT_PORT) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(g_listen_sock, 20);

    printf("[SERVER] Modular Server running on %d...\n", DEFAULT_PORT);

    // Accept Loop
    while(g_running) {
        SOCKET client = accept(g_listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        SOCKET *arg = malloc(sizeof(SOCKET));
        *arg = client;
        CreateThread(NULL, 0, client_thread, arg, 0, NULL);
    }

    // Cleanup
    topic_svc_cleanup();
    file_svc_cleanup();
    db_close();
    WSACleanup();
    return 0;
}