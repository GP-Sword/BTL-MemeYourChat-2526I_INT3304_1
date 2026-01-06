#include "net_logic.h"
#include "state.h"
#include "../libs/common/protocol.h"  
#include "../libs/common/net_utils.h" 


#include <thread>
#include <sstream>
#include <vector>
#include <string>
#include <iostream>
#include <filesystem> 

// --- 1. KHAI BÁO CROSS-PLATFORM ---
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <direct.h>   // Cho _mkdir
    
    #define MKDIR(dir) _mkdir(dir)
    #define F_TELL _ftelli64
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <sys/stat.h> // Cho mkdir
    #include <string.h>
    
    // Định nghĩa lại các kiểu/hàm của Windows sang Linux
    // Dùng #ifndef để tránh warning nếu đã được define trong os_defs.h
    
    #ifndef SOCKET
    #define SOCKET int
    #endif

    #ifndef INVALID_SOCKET
    #define INVALID_SOCKET -1
    #endif

    #ifndef SOCKET_ERROR
    #define SOCKET_ERROR -1
    #endif

    #ifndef closesocket
    #define closesocket close
    #endif
    
    // Linux mkdir cần mode (0777: quyền đọc ghi đầy đủ)
    // Macro này chưa có trong os_defs.h nên cứ define bình thường
    #define MKDIR(dir) mkdir(dir, 0777)
    #define F_TELL ftell
#endif

SOCKET g_sock = INVALID_SOCKET;
volatile bool g_net_running = false;
extern AppState g_State;

void ParseAndAddMessage(const std::string& target_id, const std::string& sender_id, const std::string& payload, bool is_history) {
    std::lock_guard<std::mutex> lock(g_State.data_mutex);

    long long timestamp = time(NULL);
    std::string final_sender = sender_id;
    std::string content = payload;
    std::string kind = "MSG"; 

    if (is_history) {
        std::stringstream ss(payload);
        std::string segment;
        std::vector<std::string> parts;
        while(std::getline(ss, segment, '|')) {
            parts.push_back(segment);
        }
        if (parts.size() >= 4) {
            try { timestamp = std::stoll(parts[0]); } catch(...) { timestamp = 0; }
            final_sender = parts[1];
            kind = parts[2];
            size_t content_pos = parts[0].length() + parts[1].length() + parts[2].length() + 3;
            if (content_pos < payload.length()) content = payload.substr(content_pos);
            else content = "";
        }
    }

    std::string chat_id = target_id;
    if (chat_id.find("group/") == 0) chat_id = target_id; 
    else if (chat_id.find("user/") == 0) {
        if (final_sender == std::string(g_State.username)) chat_id = target_id; 
        else chat_id = "user/" + final_sender;
    }

    if (g_State.conversations.find(chat_id) == g_State.conversations.end()) {
        Conversation new_conv;
        new_conv.id = chat_id;
        if (chat_id.find("group/") == 0) new_conv.name = "# " + chat_id.substr(6);
        else if (chat_id.find("user/") == 0) new_conv.name = chat_id.substr(5);
        else new_conv.name = chat_id;
        g_State.conversations[chat_id] = new_conv;
    }

    Message msg;
    msg.timestamp = timestamp;
    msg.sender = final_sender;
    msg.content = content;
    msg.is_history = is_history;
    msg.is_file = false;
    msg.file_name = "";
    msg.download_id = "";

    if (kind == "FILE" || content.find("To download: /download ") != std::string::npos) {
        msg.is_file = true;
        size_t name_start = content.find("File: ");
        size_t name_end = content.find(" (Size:");
        if (name_start != std::string::npos && name_end != std::string::npos) {
            msg.file_name = content.substr(name_start + 6, name_end - (name_start + 6));
        } else { msg.file_name = "Unknown File"; }

        size_t cmd_start = content.find("/download ");
        if (cmd_start != std::string::npos) {
            msg.download_id = content.substr(cmd_start + 10);
            while (!msg.download_id.empty() && (msg.download_id.back() == '\n' || msg.download_id.back() == '\r')) {
                msg.download_id.pop_back();
            }
        }
    }
    g_State.conversations[chat_id].messages.push_back(msg);
}

void ReceiverLoop() {
    while (g_net_running && g_sock != INVALID_SOCKET) {
        PacketHeader hdr;
        int received = recv_all(g_sock, &hdr, sizeof(PacketHeader));
        
        if (received <= 0) {
            // IN LỖI CHI TIẾT
            // int err = WSAGetLastError();
            // std::cout << "[CLIENT] Disconnected. recv return: " << received << ", Error Code: " << err << "\n";
            g_net_running = false;
            g_State.is_logged_in = false; 
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

        // --- XỬ LÝ LOGIC ---
        if (hdr.type == LTM_ERROR) {
            std::cout << "[SERVER MSG] " << str_payload << "\n";
            
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            // Cập nhật trạng thái để hiển thị lên UI
            if (str_payload == "REGISTER_OK") {
                g_State.login_status = "Registration successful! Please Login.";
            } else if (str_payload == "REGISTER_FAILED") {
                g_State.login_status = "Registration failed (User exists?).";
            } else if (str_payload == "INVALID_PASSWORD") {
                g_State.login_status = "Invalid Password.";
                g_State.is_logged_in = false;
            } else if (str_payload == "USER_NOT_EXISTS") {
                g_State.login_status = "User does not exist.";
                g_State.is_logged_in = false;
            } else {
                g_State.login_status = str_payload;
            }
        } 
        else if (hdr.type == LTM_MESSAGE) {
            ParseAndAddMessage(hdr.target_id, hdr.sender_id, str_payload, false);
        }
        else if (hdr.type == LTM_HISTORY) {
            ParseAndAddMessage(hdr.target_id, "HISTORY", str_payload, true);
        }
        else if (hdr.type == LTM_JOIN_GRP) {
            std::string topic = hdr.target_id;
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            if (g_State.conversations.find(topic) == g_State.conversations.end()) {
                Conversation new_conv;
                new_conv.id = topic;
                if (topic.find("group/") == 0) new_conv.name = "# " + topic.substr(6);
                else if (topic.find("user/") == 0) new_conv.name = topic.substr(5);
                else new_conv.name = topic;
                g_State.conversations[topic] = new_conv;
            }
        }
        else if (hdr.type == LTM_ERROR) {
            std::cout << "[SERVER ERROR] " << str_payload << "\n";
            if (str_payload == "INVALID_PASSWORD" || str_payload == "USER_NOT_EXISTS") {
                g_State.is_logged_in = false; 
            }
        }
        else if (hdr.type == LTM_FILE_META) {
            std::string p = str_payload;
            // Parse thông tin file
            std::string fname = p.substr(0, p.find('|'));
            unsigned long long size = 0;
            try {
                size = std::stoull(p.substr(p.find('|') + 1));
            } catch (...) {}

            // --- THÊM ĐOẠN NÀY ---
            // Nếu người gửi KHÁC "SERVER", tức là một User khác đang gửi file vào nhóm/PM
            // Ta cần hiển thị bong bóng chat để người nhận biết.
            if (std::string(hdr.sender_id) != "SERVER") {
                // Tạo một nội dung giả lập giống format của Server log để hàm Parse tự nhận diện là File
                // Format: "File: [Tên] (Size: [Size]). To download: /download [Tên]"
                std::string fake_content = "File: " + fname + " (Size: " + std::to_string(size) + "). To download: /download " + fname;
                
                // Thêm vào UI
                ParseAndAddMessage(hdr.target_id, hdr.sender_id, fake_content, false);
            }
            // ---------------------

            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            _mkdir("downloads");
            std::string save_path = "downloads/" + fname;
            
            // Logic cũ: Tự động lưu file đang stream tới vào ổ cứng
            g_State.download_state.fp = fopen(save_path.c_str(), "wb");
            g_State.download_state.filename = fname;
            g_State.download_state.total_size = size;
            g_State.download_state.received_size = 0;
            g_State.download_state.is_downloading = true;
        }
        else if (hdr.type == LTM_FILE_CHUNK) {
             std::lock_guard<std::mutex> lock(g_State.data_mutex);
             if (g_State.download_state.is_downloading && g_State.download_state.fp) {
                fwrite(payload, 1, hdr.payload_size, g_State.download_state.fp);
                g_State.download_state.received_size += hdr.payload_size;
                if (g_State.download_state.received_size >= g_State.download_state.total_size) {
                    fclose(g_State.download_state.fp);
                    g_State.download_state.fp = NULL;
                    g_State.download_state.is_downloading = false;
                }
             }
        }
        else if (hdr.type == LTM_LOGIN) {
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            if (g_State.conversations.find("group/global") == g_State.conversations.end()) {
                 g_State.conversations["group/global"] = {"group/global", "# global", {}};
                 g_State.current_chat_id = "group/global";
            }
        }

        delete[] payload;
    }
}

// --- NETWORK FUNCTIONS (ĐÃ SỬA MEMSET) ---

bool Net_Connect(const char* ip, int port) {
    #ifdef _WIN32
        WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); // Chỉ Windows mới cần dòng này
    #endif
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) return false;
    
    sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(g_sock); return false;
    }
    g_net_running = true;
    std::thread(ReceiverLoop).detach();
    return true;
}

void Net_Login(const char* user, const char* pass) {
    char payload[128]; 
    snprintf(payload, sizeof(payload), "LOGIN|%s", pass);
    
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr)); // Xóa sạch rác bộ nhớ
    
    hdr.type = LTM_LOGIN;
    hdr.payload_size = (uint32_t)strlen(payload);
    strcpy(hdr.target_id, "server");
    strcpy(hdr.sender_id, user);

    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, payload, hdr.payload_size);
}

void Net_Register(const char* user, const char* pass) {
    // Payload chỉ chứa password (theo logic server.c)
    // Server sẽ lấy sender_id làm username
    
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    hdr.type = LTM_REGISTER;
    hdr.payload_size = (uint32_t)strlen(pass);
    strcpy(hdr.target_id, "server");
    strcpy(hdr.sender_id, user);

    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, pass, hdr.payload_size);
}

void Net_Disconnect() {
    g_net_running = false;
    if(g_sock != INVALID_SOCKET) { 
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup(); // Chỉ Cleanup trên Windows
#endif
    }
}

void Net_SendMessage(const char* target, const char* content) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = LTM_MESSAGE;
    hdr.payload_size = (uint32_t)strlen(content);
    strcpy(hdr.target_id, target); 
    strcpy(hdr.sender_id, g_State.username);
    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, content, hdr.payload_size);
}

void Net_JoinGroup(const char* group_name) {
    char topic[64]; snprintf(topic, sizeof(topic), "group/%s", group_name);
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = LTM_JOIN_GRP;
    strcpy(hdr.target_id, topic); 
    strcpy(hdr.sender_id, g_State.username);
    send_all(g_sock, &hdr, sizeof(hdr));

    std::lock_guard<std::mutex> lock(g_State.data_mutex);
    if (g_State.conversations.find(topic) == g_State.conversations.end()) 
        g_State.conversations[topic] = {topic, "# " + std::string(group_name), {}};
    g_State.current_chat_id = topic;
}

void Net_SendFile(const char* target_id, const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return;
    std::string path_str = filepath;
    std::string filename = path_str.substr(path_str.find_last_of("/\\") + 1);
    fseek(f, 0, SEEK_END);
    unsigned long long size = (unsigned long long)F_TELL(f); // <--- DÙNG MACRO F_TELL
    fseek(f, 0, SEEK_SET);

    char meta[512];
    snprintf(meta, sizeof(meta), "%s|%llu", filename.c_str(), size);
    
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = LTM_FILE_META;
    hdr.payload_size = (uint32_t)strlen(meta);
    strcpy(hdr.target_id, target_id);
    strcpy(hdr.sender_id, g_State.username);
    
    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, meta, hdr.payload_size);

    char buf[1024]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        PacketHeader chunk;
        memset(&chunk, 0, sizeof(chunk));
        chunk.type = LTM_FILE_CHUNK;
        chunk.payload_size = (uint32_t)n;
        strcpy(chunk.target_id, target_id);
        strcpy(chunk.sender_id, g_State.username);
        send_all(g_sock, &chunk, sizeof(chunk));
        send_all(g_sock, buf, n);
        
        // Thay Sleep(1) bằng chuẩn C++ để chạy cả 2 OS
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fclose(f);
}

void Net_RequestDownload(const char* target_id, const char* file_server_name) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = LTM_DOWNLOAD;
    hdr.payload_size = (uint32_t)strlen(file_server_name);
    strcpy(hdr.target_id, target_id);
    strcpy(hdr.sender_id, g_State.username);
    send_all(g_sock, &hdr, sizeof(hdr));
    send_all(g_sock, file_server_name, hdr.payload_size);
}