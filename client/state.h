#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>

// Cấu trúc 1 tin nhắn
struct Message {
    long long timestamp;
    std::string sender;
<<<<<<< HEAD
    std::string content;     
    bool is_file;            
    std::string file_name;   
    std::string download_id; 
    bool is_history;
=======
    std::string content;
    bool is_file;      // Nếu là tin nhắn file
    bool is_history;   // Có phải tin lịch sử load lại không
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
};

// Cấu trúc 1 cuộc trò chuyện (Group hoặc User)
struct Conversation {
    std::string id;         // VD: "group/global" hoặc "user/Bob"
    std::string name;       // Tên hiển thị: "Global Chat" hoặc "Bob"
    std::vector<Message> messages;
<<<<<<< HEAD
};

struct DownloadState {
    bool is_downloading;
    std::string filename;
    unsigned long long total_size;
    unsigned long long received_size;
    FILE* fp;
=======
    bool has_unread;
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
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
<<<<<<< HEAD
    std::string current_chat_id = "";
    
    char input_buffer[1024] = "";
    
    // --- UI INPUTS ---
    char search_buffer[64] = "";    // Dùng cho Group
    char pm_search_buffer[64] = ""; // Dùng cho User (MỚI)
=======
    
    std::string current_chat_id = ""; // ID của nhóm đang chọn xem
    char input_buffer[1024] = "";     // Ô nhập tin nhắn
    char search_buffer[64] = "";      // Ô tìm kiếm/Thêm nhóm
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))

    std::mutex data_mutex; // Khóa an toàn cho luồng
};

extern AppState g_State;