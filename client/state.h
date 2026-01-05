#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>

// Cấu trúc 1 tin nhắn
struct Message {
    long long timestamp;
    std::string sender;
    std::string content;
    bool is_file;      // Nếu là tin nhắn file
    bool is_history;   // Có phải tin lịch sử load lại không
};

// Cấu trúc 1 cuộc trò chuyện (Group hoặc User)
struct Conversation {
    std::string id;         // VD: "group/global" hoặc "user/Bob"
    std::string name;       // Tên hiển thị: "Global Chat" hoặc "Bob"
    std::vector<Message> messages;
    bool has_unread;
};

struct AppState {
    // Thông tin đăng nhập
    char server_ip[32] = "127.0.0.1";
    int server_port = 910;
    char username[32] = "";
    char password[32] = "";
    bool is_logged_in = false;

    // Chat Data
    // Map lưu trữ: Key là ID cuộc trò chuyện (vd: "group/global") -> Value là dữ liệu
    std::map<std::string, Conversation> conversations;
    
    std::string current_chat_id = ""; // ID của nhóm đang chọn xem
    char input_buffer[1024] = "";     // Ô nhập tin nhắn
    char search_buffer[64] = "";      // Ô tìm kiếm/Thêm nhóm

    std::mutex data_mutex; // Khóa an toàn cho luồng
};

extern AppState g_State;