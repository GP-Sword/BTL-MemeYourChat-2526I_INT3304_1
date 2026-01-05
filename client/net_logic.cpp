// src_client/net_logic.cpp
#include "net_logic.h"
#include "state.h"
#include "../common/protocol.h"
#include "../common/net_utils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>

SOCKET g_sock = INVALID_SOCKET;
extern AppState g_State; // Link tới biến ở main
volatile bool g_net_running = true;

// Hàm nhận tin chạy ngầm
void ReceiverLoop() {
    while (g_net_running && g_sock != INVALID_SOCKET) {
        PacketHeader hdr;
        if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) <= 0) break;

        char* payload = new char[hdr.payload_size + 1];
        if (hdr.payload_size > 0) {
            recv_all(g_sock, payload, hdr.payload_size);
            payload[hdr.payload_size] = '\0';
        } else {
            payload[0] = '\0';
        }

        // --- UPDATE STATE (THREAD SAFE) ---
        if (hdr.type == LTM_MESSAGE || hdr.type == LTM_HISTORY) {
            std::lock_guard<std::mutex> lock(g_State.msg_mutex);
            g_State.messages.push_back({
                hdr.sender_id, 
                hdr.target_id, 
                std::string(payload), 
                (hdr.type == LTM_HISTORY)
            });
        }
        delete[] payload;
    }
}

bool Net_Connect(const char* ip, int port) {
    // (Giữ nguyên logic tạo socket của bạn)
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) < 0) return false;

    // Start receiver thread
    g_net_running = true;
    std::thread(ReceiverLoop).detach();
    return true;
}

void Net_Login(const char* user, const char* pass) {
    // Gửi gói tin Login như code cũ của bạn
    // Ở đây demo nhanh
    PacketHeader hdr = {LTM_LOGIN, (uint32_t)strlen(pass), "server", ""};
    strcpy(hdr.sender_id, user);
    send(g_sock, (char*)&hdr, sizeof(hdr), 0);
    send(g_sock, pass, strlen(pass), 0);
}

void Net_SendMessage(const char* target, const char* content) {
    PacketHeader hdr = {LTM_MESSAGE, (uint32_t)strlen(content), "", ""};
    strcpy(hdr.target_id, target);
    strcpy(hdr.sender_id, g_State.username);
    
    send(g_sock, (char*)&hdr, sizeof(hdr), 0);
    send(g_sock, content, strlen(content), 0);
}