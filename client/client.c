#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "../common/protocol.h"
#include "../common/net_utils.h"

#define DEFAULT_PORT 5555
#define INPUT_BUF_SIZE 1024

static SOCKET g_sock = INVALID_SOCKET;
static char g_user_id[MAX_ID_LEN] = {0};
static char g_current_group[64] = "group/global";
static volatile int g_running = 1;

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
                printf("[%s -> %s] %s\n", hdr.sender_id, hdr.target_id, payload);
                break;
            case LTM_HISTORY:
                printf("(HISTORY) %s\n", payload);
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

    HANDLE hThread = CreateThread(NULL, 0, receiver_thread, NULL, 0, NULL);
    if (hThread == NULL) {
        printf("CreateThread failed\n");
        closesocket(g_sock);
        return -1;
    }
    CloseHandle(hThread);

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

static void show_help(void) {
    printf("Commands:\n");
    printf("  /join <groupName>         Join a group (topic = group/<groupName>)\n");
    printf("  /pm <userId> <message>    Private message (topic = user/<userId>)\n");
    printf("  /group <groupName>        Set current group (for normal messages)\n");
    printf("  /quit                     Exit\n");
    printf("  <text>                    Send to current group (default group/global)\n");
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

    printf("Enter your user ID: ");
    if (!fgets(g_user_id, sizeof(g_user_id), stdin)) {
        WSACleanup();
        return 1;
    }
    size_t len = strlen(g_user_id);
    if (len > 0 && (g_user_id[len - 1] == '\n' || g_user_id[len - 1] == '\r')) {
        g_user_id[len - 1] = '\0';
    }

    if (connect_to_server(server_ip, port) != 0) {
        WSACleanup();
        return 1;
    }

    // Gửi LOGIN (không cần payload, sender_id đã có)
    if (send_packet(LTM_LOGIN, NULL, NULL) != 0) {
        printf("[CLIENT] Failed to send LOGIN.\n");
        closesocket(g_sock);
        WSACleanup();
        return 1;
    }

    printf("[CLIENT] Logged in as %s. Current group: %s\n",
           g_user_id, g_current_group);
    show_help();

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
                printf("[CLIENT] Joined group %s\n", topic_id);
            }
        } else if (strncmp(input, "/group ", 7) == 0) {
            char *group_name = input + 7;
            if (*group_name == '\0') {
                printf("Usage: /group <groupName>\n");
                continue;
            }
            _snprintf(g_current_group, sizeof(g_current_group),
                      "group/%s", group_name);
            printf("[CLIENT] Current group set to %s\n", g_current_group);
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
