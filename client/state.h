#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <winsock2.h>

struct Message {
    long long timestamp;
    std::string sender;
    std::string content;     
    bool is_file;            
    std::string file_name;   
    std::string download_id; 
    bool is_history;         // <--- THÊM DÒNG NÀY
};

struct Conversation {
    std::string id;
    std::string name;
    std::vector<Message> messages;
    // Lưu ý: Đã xóa 'bool has_unread' để đơn giản hóa, 
    // nên khi khởi tạo Conversation chỉ dùng 3 tham số.
};

struct DownloadState {
    bool is_downloading;
    std::string filename;
    unsigned long long total_size;
    unsigned long long received_size;
    FILE* fp;
};

struct AppState {
    char server_ip[32] = "127.0.0.1";
    int server_port = 910;
    char username[32] = "";
    char password[32] = "";
    bool is_logged_in = false;

    std::map<std::string, Conversation> conversations;
    std::string current_chat_id = "";
    char input_buffer[1024] = "";
    char search_buffer[64] = "";

    DownloadState download_state = {false, "", 0, 0, NULL};

    std::mutex data_mutex;
};

extern AppState g_State;