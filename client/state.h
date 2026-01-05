#pragma once
#include <vector>
#include <string>
#include <mutex>

struct ChatMessage {
    std::string sender;
    std::string target; // Group or UserID
    std::string content;
    bool is_history;
};

struct AppState {
    // Dữ liệu đăng nhập
    char username[32] = "";
    char password[32] = "";
    bool is_logged_in = false;
    char server_ip[32] = "127.0.0.1";
    int server_port = 910;

    // Dữ liệu Chat
    char current_group[64] = "group/global"; // Nhóm đang chọn xem
    char input_buffer[1024] = "";            // Ô nhập liệu
    
    // Danh sách tin nhắn (Cần mutex bảo vệ)
    std::vector<ChatMessage> messages;
    std::mutex msg_mutex;

    // Danh sách nhóm/contact (Hardcode demo, sau này server gửi về)
    std::vector<std::string> groups = {"group/global", "group/dev", "group/game"};
};

extern AppState g_State; // Biến toàn cục khai báo ở main.cpp