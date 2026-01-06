#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>

#ifdef _WIN32
    #include <winsock2.h>
#else
    // Linux không dùng winsock2, chỉ cần các kiểu dữ liệu cơ bản
    #include <netinet/in.h>
#endif

struct Message {
    long long timestamp;
    std::string sender;
    std::string content;     
    bool is_file;            
    std::string file_name;   
    std::string download_id; 
    bool is_history;
};

struct Conversation {
    std::string id;
    std::string name;
    std::vector<Message> messages;
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

    std::string login_status = ""; // Để hiện thông báo lỗi/thành công ở màn hình Login

    std::map<std::string, Conversation> conversations;
    std::string current_chat_id = "";
    
    char input_buffer[1024] = "";
    
    // --- UI INPUTS ---
    char search_buffer[64] = "";    // Dùng cho Group
    char pm_search_buffer[64] = ""; // Dùng cho User (MỚI)

    DownloadState download_state = {false, "", 0, 0, NULL};

    std::mutex data_mutex;
};

extern AppState g_State;