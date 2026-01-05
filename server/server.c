#define _CRT_SECURE_NO_WARNINGS // helps run unstab;e functions in old vers of C like strrcpy fopen
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <direct.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "../libs/protocol.h"
#include "../libs/sqlite.h"
#include "../libs/net_utils.h"

#define DEFAULT_PORT 910
#define BACKLOG 20
#define TOPIC_NAME_LEN 64

#define DATA_DIR "resources"
#define UPLOAD_DIR "uploads"
#define MAX_LOG_LINE 1024
#define MAX_LOG_PATH 260
#define MAX_UPLOAD_PATH 260
#define MAX_HISTORY_LINES 50

typedef struct SubscriberNode {
    SOCKET sock;
    struct SubscriberNode *next;
} SubscriberNode;

typedef struct Topic {
    char name[TOPIC_NAME_LEN];
    SubscriberNode *subs;
    struct Topic *next;
} Topic;

static Topic *g_topics = NULL;
static CRITICAL_SECTION g_topics_lock;
static volatile int g_running = 1;
static SOCKET g_listen_sock = INVALID_SOCKET;

typedef struct FileUpload { // no idea how ts works
    SOCKET sock;
    unsigned long long expected_size;
    unsigned long long received_size;
    FILE *fp;
    char saved_path[MAX_UPLOAD_PATH];
    char topic[TOPIC_NAME_LEN];
    char sender_id[MAX_ID_LEN];
    struct FileUpload *next;
} FileUpload;

static FileUpload *g_uploads = NULL;
static CRITICAL_SECTION g_uploads_lock;

static int parse_login_payload(const char *payload, char *mode, size_t mode_len, char *password, size_t pw_len) {
    if (!payload || !*payload) {
        strncpy(mode, "CHECK", mode_len - 1); // CHECK : does username exist in db
        mode[mode_len - 1] = '\0';
        if (pw_len > 0) password[0] = '\0';
        return 0;
    }

    const char *sep = strchr(payload, '|');
    if (!sep) {
        strncpy(mode, payload, mode_len - 1);
        mode[mode_len - 1] = '\0';
        if (pw_len > 0) password[0] = '\0';
        return 0;
    }
    size_t mlen = (size_t)(sep - payload);
    if (mlen > mode_len) mlen = mode_len - 1;
    memcpy(mode, payload, mlen);
    mode[mlen] = '\0';

    const char *pwstart = sep + 1;
    strncpy(password, pwstart, pw_len - 1);
    password[pw_len - 1] = '\0';

    return 0;
}

static void send_error_msg(SOCKET sock, const char *msg) {
    PacketHeader err;
    memset(&err, 0, sizeof(err));
    err.type = (uint8_t)LTM_ERROR;
    strncpy(err.sender_id, "SERVER", MAX_ID_LEN - 1);
    err.payload_size = (uint32_t)strlen(msg);
    send_all(sock, &err, sizeof(err));
    if(err.payload_size > 0) {
        send_all(sock, msg, (int)err.payload_size);
    }
}

static void ensure_dir(const char *dir_name) {
    _mkdir(dir_name); // helper function tạo thư mục
}

static void make_group_dirs_from_topic(const char *topic, char *group_name_out, size_t group_name_len, char *logs_dir_out, size_t logs_dir_len, char *files_dir_out, size_t files_dir_len) {
    const char *group_name = topic;
    if (strncmp(topic, "group/", 6) == 0 && topic[6] != '\0') {
        group_name = topic + 6;
    }
    char safe[TOPIC_NAME_LEN];
    strncpy(safe, group_name, sizeof(safe) - 1);
    safe[sizeof(safe) - 1] = '\0';
    for (int i = 0; safe[i] != '\0'; ++i) {
        if (safe[i] == '/' || safe[i] == '\\' || safe[i] == ':') {
            safe[i] = '_';
        }
    }
    if (group_name_out && group_name_len > 0) {
        strncpy(group_name_out, safe, group_name_len - 1);
        group_name_out[group_name_len - 1] = '\0';
    }
    char group_dir[MAX_LOG_PATH];
    _snprintf(group_dir, sizeof(group_dir), DATA_DIR"/%s", safe);
    if (logs_dir_out && logs_dir_len > 0) {
        _snprintf(logs_dir_out, logs_dir_len,
                  DATA_DIR"/%s/logs", safe);
    }
    if (files_dir_out && files_dir_len > 0) {
        _snprintf(files_dir_out, files_dir_len,
                  DATA_DIR"/%s/files", safe);
                  ensure_dir(DATA_DIR);
    }
    ensure_dir(DATA_DIR);          // resources
    ensure_dir(group_dir);         // resources/<group>
    if (logs_dir_out && logs_dir_out[0]) {
        ensure_dir(logs_dir_out);  // resources/<group>/logs
    }
    if (files_dir_out && files_dir_out[0]) {
        ensure_dir(files_dir_out); // resources/<group>/files
    }
}

static void make_topic_log_path(const char *topic, char *out, size_t out_len) {
    char group_name[TOPIC_NAME_LEN];
    char logs_dir[MAX_LOG_PATH];
    char files_dir[MAX_LOG_PATH];
    make_group_dirs_from_topic(
        topic,
        group_name, sizeof(group_name),
        logs_dir, sizeof(logs_dir),
        files_dir, sizeof(files_dir)
    );
    _snprintf(out, out_len, "%s/history.log", logs_dir);
}

static void log_history(const char *topic, const char *sender, const char *kind, const char *content) {
    ensure_dir(DATA_DIR);
    char path[MAX_LOG_PATH];
    make_topic_log_path(topic, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        printf("[SERVER] Cannot open log file %s\n", path);
        return;
    }
    time_t now = time(NULL);
    fprintf(f, "%lld|%s|%s|%s\n", 
            (long long) now, 
            sender ? sender : "",
            kind ? kind : "MSG",
            content ? content : ""    
        );
    fclose(f);
}

static void replay_history(SOCKET client_sock, const char *topic) {
    ensure_dir(DATA_DIR);
    char path[MAX_LOG_PATH];
    make_topic_log_path(topic, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        // aint no file here boy
        printf("Where my file man\n");
        return;
    }
    char line_buf[MAX_LOG_LINE];
    int total_lines = 0;

    while (fgets(line_buf, sizeof(line_buf), f)) {
        total_lines++;
    }
    rewind(f);
    int skip = 0;
    if (total_lines > MAX_HISTORY_LINES) {
        skip = total_lines - MAX_HISTORY_LINES;
    }

    int index = 0;
    while (fgets(line_buf, sizeof(line_buf), f)) {
        if (index < skip) {
            index++;
            continue;
        }
        size_t len = strlen(line_buf);
        PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.type = (uint8_t)LTM_HISTORY;
        hdr.payload_size = (uint32_t)len;
        strncpy(hdr.target_id, topic, MAX_ID_LEN - 1);
        hdr.target_id[MAX_ID_LEN - 1] = '\0';
        strncpy(hdr.sender_id, "HISTORY", MAX_ID_LEN - 1);
        hdr.sender_id[MAX_ID_LEN - 1] = '\0';

        send_all(client_sock, &hdr, sizeof(hdr));
        if (len > 0) {
            send_all(client_sock, line_buf, (int)len);
        }
        index++;
    }
    fclose(f);
}

static Topic *find_topic(const char *name) {
    Topic *t = g_topics;
    while(t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    return NULL;
}

static Topic *get_or_create_topic(const char *name) {
    Topic *t = find_topic(name);
    if (t) return t;

    t = (Topic *)malloc(sizeof(Topic));
    if (!t) return NULL;
    strncpy(t->name, name, TOPIC_NAME_LEN - 1);
    t->name[TOPIC_NAME_LEN-1] = '\0';
    t->subs = NULL;

    t->next = g_topics;
    g_topics = t;
    return t;
}

static void subscribe_socket_to_topic(SOCKET sock, const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = get_or_create_topic(topic_name);
    if (!t) { // no topic
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    SubscriberNode *cur = t->subs;
    while(cur) {
        if (cur->sock == sock) {
            LeaveCriticalSection(&g_topics_lock);
            return;
        }
        cur = cur->next;
    }

    SubscriberNode *node = (SubscriberNode *)malloc(sizeof(SubscriberNode));
    if (!node) {
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    node->sock = sock;
    node->next = t->subs;
    t->subs = node;

    printf("[SERVER] Socket %d subscribed to topic %s\n", (int)sock, topic_name);
    LeaveCriticalSection(&g_topics_lock);
}

static void unsubscribe_socket_from_topic(SOCKET sock, const char *topic_name) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = find_topic(topic_name);
    if (!t) {
        LeaveCriticalSection(&g_topics_lock);
        return;
    }
    SubscriberNode **cur = &t->subs;
    while (*cur) {
        if ((*cur)->sock == sock) {
            SubscriberNode *tmp = *cur;
            *cur = (*cur)->next;
            free(tmp);
            break;
        }
        cur = &((*cur)->next);
    }
    LeaveCriticalSection(&g_topics_lock);
}

static void remove_socket_from_all_topics(SOCKET sock) {
    EnterCriticalSection(&g_topics_lock);
    Topic *t = g_topics;
    while (t) {
        SubscriberNode **cur = &t->subs;
        while (*cur) {
            if ((*cur)->sock == sock) {
                SubscriberNode *tmp = *cur;
                *cur = (*cur)->next;
                free(tmp);
                continue;
            }
            cur = &((*cur)->next);
        }
        t = t->next;
    }
    LeaveCriticalSection(&g_topics_lock);
}

static void cancel_uploads_for_socket(SOCKET sock) {
    EnterCriticalSection(&g_uploads_lock);

    FileUpload *cur = g_uploads;
    FileUpload *prev = NULL;

    while (cur) {
        if (cur->sock == sock) {
            if (cur->fp) {
                fclose(cur->fp);
            }
            if (prev) {
                prev->next = cur->next;
            } else {
                g_uploads = cur->next;
            }
            FileUpload *to_free = cur;
            cur = cur->next;
            free(to_free);
            continue;
        }
        prev = cur;
        cur = cur->next;
    }
    LeaveCriticalSection(&g_uploads_lock);
}

static void route_message(SOCKET sender_sock, PacketHeader *hdr, const char *payload) {
    EnterCriticalSection(&g_topics_lock);

    Topic *t = find_topic(hdr->target_id);
    if (!t) {
        LeaveCriticalSection(&g_topics_lock);
        printf("[SERVER] No subscribers for topic %s\n", hdr->target_id);
        return;
    }
    SubscriberNode *cur = t->subs;
    while (cur) {
        SOCKET sock = cur->sock;
        if (sock != sender_sock) {
            if (send_all(sock, hdr, (int)sizeof(PacketHeader)) == 0) {
                if (hdr->payload_size > 0 && payload) {
                    send_all(sock, payload, (int)hdr->payload_size);
                }
            }
        }
        cur = cur->next;
    }
    LeaveCriticalSection(&g_topics_lock);
}

static int parse_file_meta(const char *payload, char *filename, size_t filename_len, unsigned long long *out_size) {
    const char *sep = strchr(payload, '|');
    if (!sep) return -1;
    size_t fn_len = (size_t)(sep - payload);
    if (fn_len >= filename_len) {
        fn_len = filename_len - 1;
    }
    memcpy(filename, payload, fn_len);
    filename[fn_len] = '\0';

    *out_size = _strtoui64(sep + 1, NULL, 10);
    return 0;
}

static void handle_file_meta(SOCKET client_sock, PacketHeader *hdr, const char *payload) {
    char filename[260] = {0};
    unsigned long long total_size = 0;
    if (parse_file_meta(payload, filename, sizeof(filename), &total_size) != 0) {
        printf("[SERVER] INVALID FILE_META payload from %s: %s\n", hdr->sender_id, payload);
        return;
    }
    char group_name[TOPIC_NAME_LEN];
    char logs_dir[MAX_LOG_PATH];
    char files_dir[MAX_LOG_PATH];
    make_group_dirs_from_topic(
        hdr->target_id,
        group_name, sizeof(group_name),
        logs_dir, sizeof(logs_dir),
        files_dir, sizeof(files_dir)
    );
    char safe_filename[260];
    strncpy(safe_filename, filename, sizeof(safe_filename) - 1);
    safe_filename[sizeof(safe_filename) - 1] = '\0';

    for (int i = 0; safe_filename[i] != 0; ++i) {
        if (safe_filename[i] == '/' || safe_filename[i] == '\\' || safe_filename[i] == ':') {
            safe_filename[i] = '_';
        }
    }
    char path[MAX_UPLOAD_PATH];
    time_t now = time(NULL);
    _snprintf(path, sizeof(path), "%s/%lld_%s", files_dir, (long long)now, safe_filename);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("[SERVER] Cannot open upload file %s\n", path);
        return;
    }

    EnterCriticalSection(&g_uploads_lock);
    FileUpload *fu = (FileUpload *)malloc(sizeof(FileUpload));
    if (!fu) {
        LeaveCriticalSection(&g_uploads_lock);
        fclose(fp);
        return;
    }

    fu->sock = client_sock;
    fu->expected_size = total_size;
    fu->received_size = 0;
    fu->fp = fp;
    strncpy(fu->saved_path, path, sizeof(fu->saved_path) - 1);
    fu->saved_path[sizeof(fu->saved_path) - 1] = '\0';
    strncpy(fu->topic, hdr->target_id, TOPIC_NAME_LEN - 1);
    fu->topic[TOPIC_NAME_LEN - 1] = '\0';
    strncpy(fu->sender_id, hdr->sender_id, MAX_ID_LEN - 1);
    fu->sender_id[MAX_ID_LEN - 1] = '\0';

    fu->next = g_uploads;
    g_uploads = fu;
    LeaveCriticalSection(&g_uploads_lock);

    // log vaof history
    char log_content[512];
    _snprintf(log_content, sizeof(log_content),
            "[FILE_META] filename=%s saved=%s size=%llu",
            filename, path, total_size);
    log_history(hdr->target_id, hdr->sender_id, "FILE", log_content);

    printf("[SERVER] Start receiving file from %s to topic %s: %s (%llu bytes)\n", 
            hdr->sender_id, hdr->target_id, filename, total_size);

    route_message(client_sock, hdr, payload);
}

static void handle_file_chunk(SOCKET client_sock, PacketHeader *hdr, const char *payload) {
    EnterCriticalSection(&g_uploads_lock);
    FileUpload *fu = g_uploads;
    FileUpload *prev = NULL;

    while (fu && fu->sock != client_sock) {
        prev = fu;
        fu = fu->next;
    }
    if (!fu) {
        LeaveCriticalSection(&g_uploads_lock);
        printf("[SERVER] FILE_CHUNK but no active upload for socket %d\n", (int)client_sock);
        return;
    }
    size_t written = fwrite(payload, 1, hdr->payload_size, fu->fp);
    fu->received_size += written;
    int finished = (fu->received_size >= fu->expected_size);
    LeaveCriticalSection(&g_uploads_lock);
    // fwd chunk to subscriber
    route_message(client_sock, hdr, payload);

    if (finished) {
        EnterCriticalSection(&g_uploads_lock);
        if (fu->fp) {
            fclose(fu->fp);
            fu->fp = NULL;
        }
        if (prev) {
            prev->next = fu->next;
        } else {
            g_uploads = fu->next;
        }
        LeaveCriticalSection(&g_uploads_lock);

        printf("[SERVER] Finished receiving file from %s, saved at %s\n", fu->sender_id, fu->saved_path);
        free(fu);
    }
}

DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET client_sock = *(SOCKET *)arg;
    free(arg);

    printf("[SERVER] Client thread started, socket %d\n", (int)client_sock);

    while (g_running) {
        PacketHeader hdr;
        if (recv_all(client_sock, &hdr, sizeof(PacketHeader)) < 0) {
            printf("[SERVER] Client %d disconnected or error.\n", (int)client_sock);
            break;
        }

        if (hdr.payload_size > MAX_PAYLOAD_SIZE && hdr.type != LTM_FILE_CHUNK) {
            printf("[SERVER] Payload too large from client %d, dropping\n", (int)client_sock);
            break;
        }

        char payload[MAX_PAYLOAD_SIZE + 1];
        memset(payload, 0, sizeof(payload));

        if (hdr.payload_size > 0) {
            if (recv_all(client_sock, payload, (int)hdr.payload_size) < 0) {
                printf("[SERVER] Failed to read payload from client %d\n", (int)client_sock);
                break;
            }  // không thêm '\0' cho FILE_CHUNK; nhưng ở đây mình vẫn +1, ok, nhưng mà gpt thêm hdr.type != LTM FILE CHUNK ở đây làm gì?
            payload[hdr.payload_size] = '\0';
        }

        switch (hdr.type) {
            case LTM_LOGIN: {
                const char *username = hdr.sender_id;
                char mode[16];
                char password[64];
                parse_login_payload(payload, mode, sizeof(mode), password, sizeof(password));
                printf("[SERVER] LOGIN from %s, mode=%s\n", username, mode);
                if (strcmp(mode, "CHECK") == 0) {
                    if (db_user_exists(username)) {
                        send_error_msg(client_sock, "USER_EXISTS");
                    } else {
                        send_error_msg(client_sock, "USER_NOT_EXISTS");
                    }
                    break;
                } 
                if (strcmp(mode, "LOGIN") == 0) {
                    if (!db_user_exists(username)) {
                        send_error_msg(client_sock, "USER_NOT_EXISTS");
                        closesocket(client_sock);
                        printf("[SERVER] Socket %d closed (unknown user)\n", (int)client_sock);
                        return 0;
                    }
                    size_t pwlen = strlen(password);
                    if (pwlen < 8 || pwlen > 12) {
                        send_error_msg(client_sock, "BAD_PASSWORD_LENGTH");
                        closesocket(client_sock);
                        printf("[SERVER] Socket %d closed (bad pw length)\n", (int)client_sock);
                        return 0;
                    }

                    if (!db_verify_user(username, password)) {
                        send_error_msg(client_sock, "INVALID_PASSWORD");
                        closesocket(client_sock);
                        printf("[SERVER] Socket %d closed (invalid password)\n", (int)client_sock);
                        return 0;
                    }
                    printf("[SERVER] User %s authenticated.\n", username);

                    // Gửi ACK LOGIN (có thể payload "OK" hoặc rỗng)
                    PacketHeader ack;
                    memset(&ack, 0, sizeof(ack));
                    ack.type = (uint8_t)LTM_LOGIN; // dùng lại LOGIN làm ACK
                    strncpy(ack.sender_id, "SERVER", MAX_ID_LEN - 1);
                    strncpy(ack.target_id, username, MAX_ID_LEN - 1);
                    ack.payload_size = 0;
                    send_all(client_sock, &ack, sizeof(ack));

                    // auto-sub topic riêng user/<id>
                    char user_topic[TOPIC_NAME_LEN];
                    _snprintf(user_topic, sizeof(user_topic), "user/%s", hdr.sender_id);
                    subscribe_socket_to_topic(client_sock, user_topic);
                    // và group mặc định
                    subscribe_socket_to_topic(client_sock, "group/global");
                    replay_history(client_sock, "group/global");
                    // replay_history(client_sock, user_topic);
                    break;
                }
                send_error_msg(client_sock, "INVALID_LOGIN_MODE");
                closesocket(client_sock);
                printf("[SERVER] Socket %d closed (invalid login mode)\n", (int)client_sock);
                return 0;
            }
            case LTM_REGISTER: {
                const char *username = hdr.sender_id;
                const char *password = payload;
                printf("[SERVER] REGISTER request for %s\n", username);

                if (db_user_exists(username)) {
                    send_error_msg(client_sock, "USER_ALREADY_EXISTS");
                    closesocket(client_sock);
                    printf("[SERVER] Socket %d closed (register existing user)\n", (int)client_sock);
                    return 0;
                }

                size_t pwlen = strlen(password);
                if (pwlen < 8 || pwlen > 12) {
                    send_error_msg(client_sock, "BAD_PASSWORD_LENGTH");
                    closesocket(client_sock);
                    printf("[SERVER] Socket %d closed (bad password length on register)\n", (int)client_sock);
                    return 0;
                }

                if (!db_create_user(username, password)) {
                    send_error_msg(client_sock, "REGISTER_FAILED");
                    closesocket(client_sock);
                    printf("[SERVER] Socket %d closed (register failed)\n", (int)client_sock);
                    return 0;
                }

                printf("[SERVER] Registered new user %s\n", username);
                send_error_msg(client_sock, "REGISTER_OK"); // dùng LTM_ERROR để chở code
                closesocket(client_sock);
                printf("[SERVER] Socket %d closed (register done, ask user to re-login)\n", (int)client_sock);
                return 0;
            }
            case LTM_JOIN_GRP:
                printf("[SERVER] %s joins group %s\n", hdr.sender_id, hdr.target_id);
                subscribe_socket_to_topic(client_sock, hdr.target_id);
                replay_history(client_sock, hdr.target_id);
                break;
            case LTM_LEAVE_GRP:
                printf("[SERVER] %s leaves group %s\n", hdr.sender_id, hdr.target_id);
                unsubscribe_socket_from_topic(client_sock, hdr.target_id);
                break;
            case LTM_MESSAGE:
                printf("[SERVER] MESSAGE from %s to %s: %s\n",
                       hdr.sender_id, hdr.target_id, payload);
                log_history(hdr.target_id, hdr.sender_id, "MSG", payload);
                route_message(client_sock, &hdr, payload);
                break;
            case LTM_FILE_META:
                handle_file_meta(client_sock, &hdr, payload);
                break;
            case LTM_FILE_CHUNK:
                handle_file_chunk(client_sock, &hdr, payload);
                break;
            default:
                printf("[SERVER] Unknown packet type %d from %s\n",
                       hdr.type, hdr.sender_id);
                break;
        }
    }
    cancel_uploads_for_socket(client_sock);
    remove_socket_from_all_topics(client_sock);
    closesocket(client_sock);
    printf("[SERVER] Socket %d closed\n", (int)client_sock);
    return 0;
}

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        printf("\n [SERVER] Ctrl+C detected, shutting down server...\n");
        printf("%d\n", g_running);
        g_running = 0;
        printf("%d\n", g_running);
        if (g_listen_sock != INVALID_SOCKET) {
            closesocket(g_listen_sock);                 // force accept() to fail
            g_listen_sock = INVALID_SOCKET;
        }
    }
    return TRUE;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }
    WSADATA wsaData; //yeah i totally know tf goin on here
    int wsa_err = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsa_err != 0) {
        printf("WSAStartup failed, error: %d\n", wsa_err);
        return 1;
    }
    if (db_init("users.db") != 0) {
        printf("[SERVER] Failed to initialize user database.\n");
        WSACleanup();
        return 1;
    }

    InitializeCriticalSection(&g_topics_lock);
    InitializeCriticalSection(&g_uploads_lock);
    SetConsoleCtrlHandler(console_handler, TRUE);

    g_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_sock == INVALID_SOCKET) {
        printf("g_listen_sock");
        printf("socket() failed, error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    int opt = 1;
    setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(g_listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind() failed, error: %d\n", WSAGetLastError());
        closesocket(g_listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(g_listen_sock, BACKLOG) == SOCKET_ERROR) {
        printf("listen() failed, error: %d\n", WSAGetLastError());
        closesocket(g_listen_sock);
        WSACleanup();
        return 1;
    }

    printf("[SERVER] Listening on port %d... \n", port);

    while(g_running) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(g_listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            printf("accept() failed, error: %d\n", WSAGetLastError());
            continue;
        }
        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        printf("[SERVER] New connection: socket %d from %s:%d\n", (int)client_sock, ip_str, ntohs(client_addr.sin_port));

        SOCKET *pSock = (SOCKET *)malloc(sizeof(SOCKET));
        if (!pSock) {
            printf("Out of memory\n");
            closesocket(client_sock);
            continue;
        }
        *pSock = client_sock;

        HANDLE hThread = CreateThread(NULL, 0, client_thread, pSock, 0, NULL);
        if (hThread == NULL) {
            printf("CreateThread giga failed\n");
            closesocket(client_sock);
            free(pSock);
            continue;
        }
        CloseHandle(hThread); // k cafan giuwx handle
    }

    DeleteCriticalSection(&g_topics_lock);
    DeleteCriticalSection(&g_uploads_lock);
    db_close();
    WSACleanup();
    return 0;
}