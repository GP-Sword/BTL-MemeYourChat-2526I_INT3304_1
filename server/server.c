#define _CRT_SECURE_NO_WARNINGS // helps run unstab;e functions in old vers of C like strrcpy fopen
#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "C:\Users\twtvf\OneDrive\Documents\GitHub\BTL-MemeYourChat-2526I_INT3304_1\common\protocol.h"
#include "C:\Users\twtvf\OneDrive\Documents\GitHub\BTL-MemeYourChat-2526I_INT3304_1\common\net_utils.h"

#define DEFAULT_PORT 910
#define BACKLOG 20
#define TOPIC_NAME_LEN 64

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

        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            printf("[SERVER] Payload too large from client %d, dropping\n", (int)client_sock);
            break;
        }

        char payload[MAX_PAYLOAD_SIZE + 1];
        memset(payload, 0, sizeof(payload));

        if (hdr.payload_size > 0) {
            if (recv_all(client_sock, payload, (int)hdr.payload_size) < 0) {
                printf("[SERVER] Failed to read payload from client %d\n", (int)client_sock);
                break;
            }
            payload[hdr.payload_size] = '\0';
        }

        switch (hdr.type) {
            case LTM_LOGIN: {
                printf("[SERVER] LOGIN from %s\n", hdr.sender_id);
                // auto-sub topic riêng user/<id>
                char user_topic[TOPIC_NAME_LEN];
                _snprintf(user_topic, sizeof(user_topic), "user/%s", hdr.sender_id);
                subscribe_socket_to_topic(client_sock, user_topic);
                // và group mặc định
                subscribe_socket_to_topic(client_sock, "group/global");
                break;
            }
            case LTM_JOIN_GRP:
                printf("[SERVER] %s joins group %s\n", hdr.sender_id, hdr.target_id);
                subscribe_socket_to_topic(client_sock, hdr.target_id);
                break;
            case LTM_LEAVE_GRP:
                printf("[SERVER] %s leaves group %s\n", hdr.sender_id, hdr.target_id);
                unsubscribe_socket_from_topic(client_sock, hdr.target_id);
                break;
            case LTM_MESSAGE:
                printf("[SERVER] MESSAGE from %s to %s: %s\n",
                       hdr.sender_id, hdr.target_id, payload);
                // sau này thêm save_history ở đây
                route_message(client_sock, &hdr, payload);
                break;
            default:
                printf("[SERVER] Unknown packet type %d from %s\n",
                       hdr.type, hdr.sender_id);
                break;
        }
    }

    remove_socket_from_all_topics(client_sock);
    closesocket(client_sock);
    printf("[SERVER] Socket %d closed\n", (int)client_sock);
    return 0;
}

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        printf("\n [SERVER] Ctrl+C detected, shutting down server...\n");
        g_running = 0;
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
    InitializeCriticalSection(&g_topics_lock);
    SetConsoleCtrlHandler(console_handler, TRUE);

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket() failed, error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind() failed, error: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(listen_sock, BACKLOG) == SOCKET_ERROR) {
        printf("listen() failed, error: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    printf("[SERVER] Listening on port %d... \n", port);

    while(g_running) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            if (!g_running) break;
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

    closesocket(listen_sock);
    DeleteCriticalSection(&g_topics_lock);
    WSACleanup();
    return 0;
}