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

#include "../libs/protocol.h"
#include "../libs/net_utils.h"

#define DEFAULT_PORT 910
#define INPUT_BUF_SIZE 1024

static SOCKET g_sock = INVALID_SOCKET;
static char g_user_id[MAX_ID_LEN] = {0};
static char g_current_group[64] = "group/global";
static volatile int g_running = 1;
static int g_auth_done = 0;

typedef struct {
    int active;
    FILE *fp;
    unsigned long long expected_size;
    unsigned long long received_size;
    char filename[260];
} FileDownloadState;

static FileDownloadState g_file_download = {0};

static void ensure_download_dir(void) {
    _mkdir("downloads");
}
//client reuse
static int parse_file_meta(const char *payload,
                           char *filename,
                           size_t filename_len,
                           unsigned long long *out_size) {
    const char *sep = strchr(payload, '|');
    if (!sep) return -1;

    size_t fn_len = (size_t)(sep - payload);
    if (fn_len >= filename_len) fn_len = filename_len - 1;
    memcpy(filename, payload, fn_len);
    filename[fn_len] = '\0';

    *out_size = _strtoui64(sep + 1, NULL, 10);
    return 0;
}

static void strip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}


static void format_time_str(long long timestamp, char *out_buf, size_t buf_size) {
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    // Format: HH:MM:SS - DD/MM/YYYY
    strftime(out_buf, buf_size, "%H:%M:%S - %d/%m/%Y", tm_info);
}

static void get_current_time_str(char *out_buf, size_t buf_size) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(out_buf, buf_size, "%H:%M:%S - %d/%m/%Y", tm_info);
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
    if (g_sock == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("inet_pton() failed\n");
        closesocket(g_sock);
        return -1;
    }

    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("connect() failed: %d\n", WSAGetLastError());
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
    if (payload) {
        hdr.payload_size = (uint32_t)strlen(payload);
    } else {
        hdr.payload_size = 0;
    }

    if (target_id) {
        strncpy(hdr.target_id, target_id, MAX_ID_LEN - 1);
        hdr.target_id[MAX_ID_LEN - 1] = '\0';
    }
    strncpy(hdr.sender_id, g_user_id, MAX_ID_LEN - 1);
    hdr.sender_id[MAX_ID_LEN - 1] = '\0';

    if (send_all(g_sock, &hdr, (int)sizeof(hdr)) < 0) return -1;
    if (hdr.payload_size > 0 && payload) {
        if (send_all(g_sock, payload, (int)hdr.payload_size) < 0) return -1;
    }
    return 0;
}

static int send_file_to_topic(const char *topic_id, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("[CLIENT] Cannot open this file: %s\n", filepath);
        return -1;
    }
    const char *p = filepath;
    const char *last_sep = filepath;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p + 1;
        }
        p++;
    }
    const char *filename = last_sep;

    fseek(fp, 0, SEEK_END);
    long size_long = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size_long < 0) {
        printf("[CLIENT] ftell failed.\n");
        fclose(fp);
        return -1;
    }
    unsigned long long total_size = (unsigned long long)size_long;

    char meta_payload[512];
    _snprintf(meta_payload, sizeof(meta_payload),
              "%s|%llu", filename, total_size);

    if (send_packet(LTM_FILE_META, topic_id, meta_payload) != 0) {
        printf("[CLIENT] Failed to send FILE_META.\n");
        fclose(fp);
        return -1;
    }

    // gửi các CHUNK binary
    char buffer[MAX_PAYLOAD_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.type = (uint8_t)LTM_FILE_CHUNK;
        hdr.payload_size = (uint32_t)n;
        strncpy(hdr.target_id, topic_id, MAX_ID_LEN - 1);
        hdr.target_id[MAX_ID_LEN - 1] = '\0';
        strncpy(hdr.sender_id, g_user_id, MAX_ID_LEN - 1);
        hdr.sender_id[MAX_ID_LEN - 1] = '\0';

        if (send_all(g_sock, &hdr, (int)sizeof(hdr)) < 0) {
            printf("[CLIENT] Failed to send FILE_CHUNK header.\n");
            fclose(fp);
            return -1;
        }
        if (send_all(g_sock, buffer, (int)n) < 0) {
            printf("[CLIENT] Failed to send FILE_CHUNK data.\n");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    printf("[CLIENT] File sent to %s: %s (%llu bytes)\n",
           topic_id, filepath, total_size);
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

static int login_register_flow(const char *server_ip, int port) {
    while (1) {
        // 1) Nhập username
        printf("Enter your user ID: ");
        if (!fgets(g_user_id, sizeof(g_user_id), stdin)) {
            return -1;
        }
        strip_newline(g_user_id);
        if (g_user_id[0] == '\0') {
            printf("Username cannot be empty.\n");
            continue;
        }

        // 2) Kết nối tới server
        if (connect_to_server(server_ip, port) != 0) {
            printf("Cannot connect to server.\n");
            return -1;
        }

        // 3) Gửi LOGIN mode=CHECK để hỏi server "user tồn tại không?"
        char check_payload[32] = "CHECK";
        if (send_packet(LTM_LOGIN, NULL, check_payload) != 0) {
            printf("Failed to send CHECK login.\n");
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return -1;
        }

        // 4) Đọc 1 packet trả lời (blocking)
        PacketHeader hdr;
        if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) < 0) {
            printf("Failed to receive response for CHECK.\n");
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return -1;
        }

        char payload[MAX_PAYLOAD_SIZE + 1];
        memset(payload, 0, sizeof(payload));
        if (hdr.payload_size > 0) {
            if (recv_all(g_sock, payload, (int)hdr.payload_size) < 0) {
                printf("Failed to read payload.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                return -1;
            }
            payload[hdr.payload_size] = '\0';
        }

        int user_exists = 0;
        if (hdr.type == LTM_ERROR && strcmp(payload, "USER_EXISTS") == 0) {
            user_exists = 1;
            printf("User exists. Please enter your password.\n");
        } else if (hdr.type == LTM_ERROR && strcmp(payload, "USER_NOT_EXISTS") == 0) {
            user_exists = 0;
            printf("User does not exist. Enter password to register new user.\n");
        } else {
            printf("Unexpected response from server during CHECK: type=%d payload=%s\n",
                   hdr.type, payload);
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return -1;
        }

        // 5) Nhập password
        char password[64];
        printf("Password: ");
        if (!fgets(password, sizeof(password), stdin)) {
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return -1;
        }
        strip_newline(password);

        size_t pwlen = strlen(password);
        if (pwlen < 8 || pwlen > 12) {
            printf("Password length must be 8-12 characters.\n");
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            continue; // quay lại từ đầu, hỏi username lại
        }

        if (user_exists) {
            // 6A) LOGIN mode
            char login_payload[128];
            _snprintf(login_payload, sizeof(login_payload),
                      "LOGIN|%s", password);

            if (send_packet(LTM_LOGIN, NULL, login_payload) != 0) {
                printf("Failed to send LOGIN.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                return -1;
            }

            // Chờ ack hoặc error
            if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) < 0) {
                printf("Failed to receive LOGIN response.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                return -1;
            }

            memset(payload, 0, sizeof(payload));
            if (hdr.payload_size > 0) {
                if (recv_all(g_sock, payload, (int)hdr.payload_size) < 0) {
                    printf("Failed to read LOGIN payload.\n");
                    closesocket(g_sock);
                    g_sock = INVALID_SOCKET;
                    return -1;
                }
                payload[hdr.payload_size] = '\0';
            }

            if (hdr.type == LTM_ERROR) {
                if (strcmp(payload, "INVALID_PASSWORD") == 0) {
                    printf("Invalid password.\n");
                } else if (strcmp(payload, "BAD_PASSWORD_LENGTH") == 0) {
                    printf("Password length must be 8-12 characters.\n");
                } else if (strcmp(payload, "USER_NOT_EXISTS") == 0) {
                    printf("User no longer exists.\n");
                } else {
                    printf("Login error: %s\n", payload);
                }

                closesocket(g_sock);
                g_sock = INVALID_SOCKET;

                char c[8];
                printf("Try again? (y/n): ");
                if (!fgets(c, sizeof(c), stdin)) return -1;
                if (c[0] != 'y' && c[0] != 'Y') {
                    return -1; // user không muốn thử nữa
                }
                continue; // quay lại từ đầu hỏi username
            }

            if (hdr.type == LTM_LOGIN) {
                // LOGIN OK – g_sock vẫn mở và sẽ dùng cho chat
                printf("[CLIENT] Logged in as %s. Current group: %s\n",
                       g_user_id, g_current_group);
                show_help();
                return 0;
            }

            printf("Unexpected packet after LOGIN: type=%d\n", hdr.type);
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return -1;
        } else {
            // 6B) REGISTER mode
            if (send_packet(LTM_REGISTER, NULL, password) != 0) {
                printf("Failed to send REGISTER.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                return -1;
            }

            if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) < 0) {
                printf("Failed to receive REGISTER response.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                return -1;
            }

            memset(payload, 0, sizeof(payload));
            if (hdr.payload_size > 0) {
                if (recv_all(g_sock, payload, (int)hdr.payload_size) < 0) {
                    printf("Failed to read REGISTER payload.\n");
                    closesocket(g_sock);
                    g_sock = INVALID_SOCKET;
                    return -1;
                }
                payload[hdr.payload_size] = '\0';
            }

            if (hdr.type == LTM_ERROR && strcmp(payload, "REGISTER_OK") == 0) {
                printf("Registration success. Please login again.\n");
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                continue; // quay lại vòng while, nhập username + login
            } else {
                printf("Register error: %s\n", payload);
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
                char c[8];
                printf("Try again? (y/n): ");
                if (!fgets(c, sizeof(c), stdin)) return -1;
                if (c[0] != 'y' && c[0] != 'Y') {
                    return -1;
                }
                continue;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    char server_ip[64] = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
    }

    // Init Winsock
    WSADATA wsaData;
    int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_err != 0) {
        printf("WSAStartup failed: %d\n", wsa_err);
        return 1;
    }

    if (login_register_flow(server_ip, port) != 0) {
        WSACleanup();
        return 0; // user logout hoặc fail
    }

    // login OK, g_sock đã connected + server đã auto-sub + replay history
    // start receiver_thread and begin chat loop

    HANDLE hThread = CreateThread(NULL, 0, receiver_thread, NULL, 0, NULL);
    if (hThread == NULL) {
        printf("CreateThread failed\n");
        closesocket(g_sock);
        WSACleanup();
        return 1;
    }
    CloseHandle(hThread);

    char input[INPUT_BUF_SIZE];

    while (g_running && fgets(input, sizeof(input), stdin)) {
        size_t in_len = strlen(input);
        if (in_len > 0 &&
            (input[in_len - 1] == '\n' || input[in_len - 1] == '\r')) {
            input[in_len - 1] = '\0';
        }

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
    }

    printf("[CLIENT] Exiting...\n");
    closesocket(g_sock);
    WSACleanup();
    return 0;
}
