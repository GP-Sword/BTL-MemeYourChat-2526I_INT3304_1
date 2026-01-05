#include "net_logic.h"
#include "state.h"
#include "../libs/common/protocol.h"  // Chú ý đường dẫn tương đối tới common
#include "../libs/common/net_utils.h" // Chú ý đường dẫn tương đối tới common
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <sstream>
#include <vector>
#include <string>
#include <iostream>

// Biến nội bộ quản lý socket
SOCKET g_sock = INVALID_SOCKET;
volatile bool g_net_running = false;

// Link tới biến toàn cục state bên main.cpp
extern AppState g_State;

// --- CÁC HÀM HỖ TRỢ NỘI BỘ (HELPER) ---

void ParseAndAddMessage(const std::string& target_id, const std::string& sender_id, const std::string& payload, bool is_history) {
    std::lock_guard<std::mutex> lock(g_State.data_mutex);

    long long timestamp = 0;
    std::string final_sender = sender_id;
    std::string kind = "MSG";
    std::string content = payload;

    // Parse payload lịch sử: timestamp|sender|kind|content
    if (is_history) {
        std::stringstream ss(payload);
        std::string segment;
        std::vector<std::string> parts;
        while(std::getline(ss, segment, '|')) {
            parts.push_back(segment);
        }

        if (parts.size() >= 4) {
            try { timestamp = std::stoll(parts[0]); } catch(...) {}
            final_sender = parts[1];
            kind = parts[2];
            // Ghép lại nội dung (đề phòng nội dung có chứa dấu |)
            size_t content_start = parts[0].length() + parts[1].length() + parts[2].length() + 3; 
            if (content_start < payload.length()) content = payload.substr(content_start);
            else content = "";
        }
    } else {
        timestamp = time(NULL);
    }

    // Logic Routing: Xác định tin nhắn thuộc về Tab chat nào
    std::string chat_id;

    if (target_id.find("group/") == 0) {
        chat_id = target_id; // Tin nhóm
    } else if (target_id.find("user/") == 0) {
        // Tin riêng
        if (final_sender == std::string(g_State.username)) {
             // Bỏ qua tin mình tự gửi trong history để tránh rối (hoặc xử lý sau)
             return; 
        } else {
            chat_id = "user/" + final_sender;
        }
    }

    // Tạo Conversation mới nếu chưa có
    if (g_State.conversations.find(chat_id) == g_State.conversations.end()) {
        Conversation new_conv;
        new_conv.id = chat_id;
        if (chat_id.find("group/") == 0) new_conv.name = "# " + chat_id.substr(6);
        else if (chat_id.find("user/") == 0) new_conv.name = chat_id.substr(5);
        else new_conv.name = chat_id;
        
        g_State.conversations[chat_id] = new_conv;
    }

    // Thêm tin nhắn vào vector
    Message msg;
    msg.timestamp = timestamp;
    msg.sender = final_sender;
    msg.content = content;
    msg.is_file = (kind == "FILE");
    msg.is_history = is_history;

    g_State.conversations[chat_id].messages.push_back(msg);
}

// Luồng nhận tin nhắn chạy ngầm
void ReceiverLoop() {
    while (g_net_running && g_sock != INVALID_SOCKET) {
        PacketHeader hdr;
        if (recv_all(g_sock, &hdr, sizeof(PacketHeader)) <= 0) {
            std::cout << "[CLIENT] Disconnected from server.\n";
            g_net_running = false;
            break;
        }

        char* payload = new char[hdr.payload_size + 1];
        if (hdr.payload_size > 0) {
            recv_all(g_sock, payload, hdr.payload_size);
            payload[hdr.payload_size] = '\0';
        } else {
            payload[0] = '\0';
        }
        std::string str_payload = std::string(payload);

        // Xử lý gói tin
        if (hdr.type == LTM_MESSAGE) {
            ParseAndAddMessage(hdr.target_id, hdr.sender_id, str_payload, false);
        }
        else if (hdr.type == LTM_HISTORY) {
            ParseAndAddMessage(hdr.target_id, "HISTORY", str_payload, true);
        }
        else if (hdr.type == LTM_LOGIN) {
            // Login thành công -> Auto join global
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            if (g_State.conversations.find("group/global") == g_State.conversations.end()) {
                 g_State.conversations["group/global"] = {"group/global", "# global", {}, false};
                 g_State.current_chat_id = "group/global";
            }
        }
        // TODO: Xử lý FILE_CHUNK, ERROR...

        delete[] payload;
    }
}

// --- TRIỂN KHAI CÁC HÀM TRONG NET_LOGIC.H ---

bool Net_Connect(const char* ip, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) return false;
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(g_sock);
        return false;
    }

    // Khởi chạy thread nhận tin
    g_net_running = true;
    std::thread(ReceiverLoop).detach();
    return true;
}

void Net_Login(const char* user, const char* pass) {
    if (g_sock == INVALID_SOCKET) return;

    // Logic Server hiện tại: Mode|Password
    // Gửi LOGIN mode "LOGIN"
    char payload[128];
    snprintf(payload, sizeof(payload), "LOGIN|%s", pass);

    PacketHeader hdr = {0};
    hdr.type = LTM_LOGIN;
    hdr.payload_size = (uint32_t)strlen(payload);
    strcpy(hdr.sender_id, user);
    strcpy(hdr.target_id, "server");

    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, payload, hdr.payload_size);
}

void Net_Disconnect() {
    g_net_running = false;
    if (g_sock != INVALID_SOCKET) {
        shutdown(g_sock, SD_BOTH);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    WSACleanup();
}

void Net_SendMessage(const char* target, const char* content) {
    if (g_sock == INVALID_SOCKET) return;

    PacketHeader hdr = {0};
    hdr.type = LTM_MESSAGE;
    hdr.payload_size = (uint32_t)strlen(content);
    strcpy(hdr.target_id, target);
    strcpy(hdr.sender_id, g_State.username);
    
    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, content, hdr.payload_size);
}

void Net_JoinGroup(const char* group_name) {
    if (g_sock == INVALID_SOCKET) return;

    char topic[64];
    snprintf(topic, sizeof(topic), "group/%s", group_name);
    
    PacketHeader hdr = {0};
    hdr.type = LTM_JOIN_GRP;
    hdr.payload_size = 0;
    strcpy(hdr.target_id, topic);
    strcpy(hdr.sender_id, g_State.username);
    
    send_all(g_sock, &hdr, sizeof(hdr));

    // Cập nhật UI ngay lập tức
    std::lock_guard<std::mutex> lock(g_State.data_mutex);
    if (g_State.conversations.find(topic) == g_State.conversations.end()) {
        std::string name = "# " + std::string(group_name);
        g_State.conversations[topic] = {topic, name, {}, false};
    }
    g_State.current_chat_id = topic;
}

void Net_SendFile(const char* target_id, const char* filepath) {
    // Tạm thời chưa implement, để trống để không lỗi Linker
    std::cout << "[CLIENT] Feature SendFile coming soon...\n";
}