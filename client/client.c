#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>

#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "../libs/common/protocol.h"
#include "../libs/common/net_utils.h"

#define DEFAULT_PORT 910
#define INPUT_BUF_SIZE 1024

static SOCKET g_sock = INVALID_SOCKET;
static char g_user_id[MAX_ID_LEN] = {0};
static char g_current_group[64] = "group/global"; // Mặc định chat vào global
static volatile int g_running = 1;

// --- Helper Functions ---

static void strip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}

static void get_current_time_str(char *out_buf, size_t buf_size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(out_buf, buf_size, "%H:%M:%S", tm_info);
}

DWORD WINAPI receiver_thread(LPVOID arg) {
    (void)arg;

    while (g_running) {
        PacketHeader hdr;
        if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) < 0) {
            printf("[CLIENT] Disconnected from server.\n");
            g_running = 0;
            break;
        }

        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            printf("[CLIENT] Payload too large, disconnecting.\n");
            g_running = 0;
            break;
        }

        char payload[MAX_PAYLOAD_SIZE + 1];
        memset(payload, 0, sizeof(payload));
        if (hdr.payload_size > 0) {
            if (recv_all(g_sock, payload, (int)hdr.payload_size) < 0) {
                printf("[CLIENT] Failed to read payload.\n");
                g_running = 0;
                break;
            }
            payload[hdr.payload_size] = '\0';
        }

        switch (hdr.type) {
            case LTM_MESSAGE:
                // get time
                char time_str[64];
                get_current_time_str(time_str, sizeof(time_str));

                printf("[%s] [%s -> %s] %s\n", time_str, hdr.sender_id, hdr.target_id, payload);
                break;
            case LTM_HISTORY:
                // 1. timestamp
                char *token = strtok(payload, "|");
                if (!token) { printf("(HISTORY RAW) %s\n", payload); break; }
                long long ts = _strtoi64(token, NULL, 10);

                // 2. sender
                token = strtok(NULL, "|");
                char *sender = token ? token : "Unknown";

                // 3. type
                token = strtok(NULL, "|");
                char *kind = token ? token : "MSG";

                // 4. content
                token = strtok(NULL, "");
                char *content = token ? token : "";

                // 5. convert time
                char time_str_history[64];
                format_time_str(ts, time_str_history, sizeof(time_str_history));

                // 6. finalize
                printf("[HISTORY] [%s] [%s] <%s>: %s\n", time_str_history, sender, kind, content);
                
                break;
            case LTM_ERROR:
                printf("[SERVER ERROR] %s\n", payload);
                break;
            default:
                printf("[CLIENT] Packet type %d, payload: %s\n",
                       hdr.type, payload);
                break;
        }
    }

    return 0;
}

static int connect_to_server(const char *ip, int port) {
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_sock);
        return -1;
    }
    printf("[CLIENT] Connected to %s:%d\n", ip, port);
    return 0;
}

static int send_packet(PacketType type, const char *target_id, const char *payload) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = (uint8_t)type;
    
    // Xử lý payload
    if (payload) hdr.payload_size = (uint32_t)strlen(payload);
    else hdr.payload_size = 0;

    // Xử lý target_id (nếu có)
    if (target_id) {
        strncpy(hdr.target_id, target_id, MAX_ID_LEN - 1);
    }
    
    // Luôn gửi sender_id là user hiện tại
    strncpy(hdr.sender_id, g_user_id, MAX_ID_LEN - 1);

    if (send_all(g_sock, &hdr, (int)sizeof(hdr)) < 0) return -1;
    if (hdr.payload_size > 0 && payload) {
        if (send_all(g_sock, payload, (int)hdr.payload_size) < 0) return -1;
    }
    return 0;
}

static int send_file_to_topic(const char *topic_id, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("[CLIENT] Error: Cannot open file %s\n", filepath);
        return -1;
    }
    
    // Lấy tên file từ đường dẫn
    const char *p = filepath, *filename = filepath;
    while (*p) {
        if (*p == '/' || *p == '\\') filename = p + 1;
        p++;
    }

    fseek(fp, 0, SEEK_END);
    unsigned long long total_size = (unsigned long long)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Gửi META
    char meta_payload[512];
    snprintf(meta_payload, sizeof(meta_payload), "%s|%llu", filename, total_size);
    send_packet(LTM_FILE_META, topic_id, meta_payload);

    // Gửi CHUNK
    char buffer[MAX_PAYLOAD_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        PacketHeader hdr = {0};
        hdr.type = LTM_FILE_CHUNK;
        hdr.payload_size = (uint32_t)n;
        strncpy(hdr.target_id, topic_id, MAX_ID_LEN - 1);
        strncpy(hdr.sender_id, g_user_id, MAX_ID_LEN - 1);

        send_all(g_sock, &hdr, sizeof(hdr));
        send_all(g_sock, buffer, (int)n);
    }
    fclose(fp);
    printf("[CLIENT] File sent to %s\n", topic_id);
    return 0;
}

// --- Thread & Flow ---

DWORD WINAPI receiver_thread(LPVOID arg) {
    (void)arg;
    while (g_running) {
        PacketHeader hdr;
        if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) < 0) {
            printf("\n[CLIENT] Server disconnected.\n");
            g_running = 0; break;
        }

        if (hdr.payload_size > MAX_PAYLOAD_SIZE) break; 
        
        char payload[MAX_PAYLOAD_SIZE + 1] = {0};
        if (hdr.payload_size > 0) {
            if (recv_all(g_sock, payload, (int)hdr.payload_size) < 0) break;
            payload[hdr.payload_size] = '\0';
        }

        switch (hdr.type) {
            case LTM_MESSAGE: {
                char time_str[64];
                get_current_time_str(time_str, sizeof(time_str));
                // Nếu tin nhắn đến từ group khác group đang xem, hiện rõ tên group
                if (strcmp(hdr.target_id, g_current_group) != 0 && strncmp(hdr.target_id, "group/", 6) == 0) {
                     printf("[%s] [%s] %s: %s\n", time_str, hdr.target_id, hdr.sender_id, payload);
                } else {
                     printf("[%s] %s: %s\n", time_str, hdr.sender_id, payload);
                }
                break;
            }
            case LTM_HISTORY:
                printf("%s\n", payload);
                break;
            case LTM_ERROR:
                printf("[SERVER ERROR] %s\n", payload);
                break;
            default:
                break;
        }
    }
    return 0;
}

static void show_help(void) {
    printf("Commands:\n");
    printf("  /help                     Display the commands again.\n");
    printf("  /group <name>             Create group if needed, set as current, and server lists all groups.\n");
    printf("  /removegroup <name>       Remove an existing group (except 'global') and list groups.\n");
    printf("  /join <groupName>         Join an existing group (topic = group/<groupName>)\n");
    printf("  /pm <userId> <message>    Private message (topic = user/<userId>)\n");
    printf("  /filepm <userId> <path>   Send file privately to user\n");
    printf("  /filegrp <group> <path>   Send file to group (topic = group/<group>)\n");
    printf("  /quit                     Exit\n");
    printf("  <text>                    Send to current group (default group/global)\n");
}

// Xử lý logic Login/Register (giữ nguyên logic cũ nhưng làm gọn)
static int auth_flow(const char *ip, int port) {
    while (1) {
        printf("Username: ");
        if (!fgets(g_user_id, sizeof(g_user_id), stdin)) return -1;
        strip_newline(g_user_id);
        if (strlen(g_user_id) == 0) continue;

        if (connect_to_server(ip, port) != 0) {
            printf("Connection failed.\n"); return -1;
        }

        // 1. Check user
        send_packet(LTM_LOGIN, NULL, "CHECK");
        
        PacketHeader hdr;
        char buf[MAX_PAYLOAD_SIZE + 1] = {0};
        recv_all(g_sock, &hdr, sizeof(hdr));
        if (hdr.payload_size) recv_all(g_sock, buf, hdr.payload_size);

        int exists = (strcmp(buf, "USER_EXISTS") == 0);
        printf("%s. Enter password: ", exists ? "User found" : "New user");
        
        char pw[64];
        fgets(pw, sizeof(pw), stdin);
        strip_newline(pw);

        // 2. Send Login or Register
        if (exists) {
            char login_payload[128];
            snprintf(login_payload, sizeof(login_payload), "LOGIN|%s", pw);
            send_packet(LTM_LOGIN, NULL, login_payload);
        } else {
            send_packet(LTM_REGISTER, NULL, pw);
        }

        // 3. Get Result
        memset(&hdr, 0, sizeof(hdr));
        memset(buf, 0, sizeof(buf));
        recv_all(g_sock, &hdr, sizeof(hdr));
        if (hdr.payload_size) recv_all(g_sock, buf, hdr.payload_size);

        if (hdr.type == LTM_LOGIN || (hdr.type == LTM_ERROR && strcmp(buf, "REGISTER_OK") == 0)) {
            if (hdr.type == LTM_LOGIN) {
                printf("Login successful!\n");
                return 0; // Success
            }
            printf("Registered. Please login again.\n");
        } else {
            printf("Auth failed: %s\n", buf);
        }
        closesocket(g_sock);
    }
}

int main(int argc, char *argv[]) {
    char ip[64] = "127.0.0.1";
    int port = DEFAULT_PORT;
    if (argc >= 2) strcpy(ip, argv[1]);
    
    // Init WSA
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);

    if (auth_flow(ip, port) != 0) return 0;

    // Start Chat
    CreateThread(NULL, 0, receiver_thread, NULL, 0, NULL);
    show_help();

    char input[INPUT_BUF_SIZE];
    while (g_running && fgets(input, sizeof(input), stdin)) {
        strip_newline(input);
        if (strlen(input) == 0) continue;

        if (strncmp(input, "/quit", 5) == 0) {
            g_running = 0;
            break;
        } else if (strncmp(input, "/join ", 6) == 0) {
            char *group_name = input + 6;
            if (*group_name == '\0') {
                printf("Usage: /join <groupName>\n");
                continue;
            }
            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "group/%s", group_name);

            if (send_packet(LTM_JOIN_GRP, topic_id, NULL) == 0) {
                // server sẽ báo lỗi nếu group không tồn tại
                strncpy(g_current_group, topic_id, sizeof(g_current_group) - 1);
                g_current_group[sizeof(g_current_group) - 1] = '\0';
                printf("[CLIENT] Joined group %s (current group set)\n", topic_id);
            }
        } else if (strncmp(input, "/help", 5) == 0) {
            show_help();
        } else if (strncmp(input, "/group ", 6) == 0) {
            char *group_name = input + 7;
            if (*group_name == '\0') {
                printf("Usage: /group <groupName>\n");
                continue;
            }

            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "group/%s", group_name);

            // yêu cầu server tạo group (nếu chưa có) & gửi danh sách group
            if (send_packet(LTM_GROUP_CMD, topic_id, "CREATE") != 0) {
                printf("[CLIENT] Failed to send group create command.\n");
            } else {
                strncpy(g_current_group, topic_id, sizeof(g_current_group) - 1);
                g_current_group[sizeof(g_current_group) - 1] = '\0';
                printf("[CLIENT] Current group set to %s\n", g_current_group);
                printf("[CLIENT] Server will list all groups in chat.\n");
            }
        } else if (strncmp(input, "/removegroup ", 13) == 0) {
            char *group_name = input + 13;
            if (*group_name == '\0') {
                printf("Usage: /removegroup <groupName>\n");
                continue;
            }

            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "group/%s", group_name);

            if (send_packet(LTM_GROUP_CMD, topic_id, "REMOVE") != 0) {
                printf("[CLIENT] Failed to send group remove command.\n");
            } else {
                printf("[CLIENT] Remove group request sent. Server will respond.\n");
            }
        } else if (strncmp(input, "/pm ", 4) == 0) {
            char *rest = input + 4;
            char *space = strchr(rest, ' ');
            if (!space) {
                printf("Usage: /pm <userId> <message>\n");
                continue;
            }
            *space = '\0';
            char *user_id = rest;
            char *msg = space + 1;

            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "user/%s", user_id);
            if (send_packet(LTM_MESSAGE, topic_id, msg) != 0) {
                printf("[CLIENT] Failed to send private message.\n");
            }
        } else if (strncmp(input, "/filepm ", 8) == 0) {
            char *rest = input + 8;
            char *space = strchr(rest, ' ');
            if (!space) {
                printf("Usage: /filepm <userId> <path>\n");
                continue;
            }
            *space = '\0';
            char *user_id = rest;
            char *path = space + 1;

            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "user/%s", user_id);
            send_file_to_topic(topic_id, path);
        } else if (strncmp(input, "/filegrp ", 9) == 0) {
            char *rest = input + 9;
            char *space = strchr(rest, ' ');
            if (!space) {
                printf("Usage: /filegrp <groupName> <path>\n");
                continue;
            }
            *space = '\0';
            char *group_name = rest;
            char *path = space + 1;

            char topic_id[MAX_ID_LEN];
            _snprintf(topic_id, sizeof(topic_id), "group/%s", group_name);
            send_file_to_topic(topic_id, path);
        } else if (input[0] == '\0') {
            continue;
        } else {
            if (send_packet(LTM_MESSAGE, g_current_group, input) != 0) {
                printf("[CLIENT] Failed to send message.\n");
            }
        }
         if (strncmp(input, "/file ", 6) == 0) {
             // Gửi file vào nhóm hiện tại
             send_file_to_topic(g_current_group, input + 6);
        }
        // --- NORMAL CHAT ---
        else {
            // Gửi tin nhắn vào g_current_group
            send_packet(LTM_MESSAGE, g_current_group, input);
        }
    }

    closesocket(g_sock);
    WSACleanup();
    return 0;
}