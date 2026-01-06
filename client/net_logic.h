#ifndef NET_LOGIC_H
#define NET_LOGIC_H

// --- 1. KẾT NỐI & TÀI KHOẢN ---
// Kết nối socket tới server
bool Net_Connect(const char* ip, int port);

// Gửi gói tin Login (hoặc Register nếu server yêu cầu)
void Net_Login(const char* user, const char* pass);

void Net_Register(const char* user, const char* pass);

// Ngắt kết nối (Dùng khi đóng app)
void Net_Disconnect();


// --- 2. CHỨC NĂNG CHAT ---
// Gửi tin nhắn văn bản (cho User hoặc Group)
// target: "group/global" hoặc "user/Alice"
void Net_SendMessage(const char* target, const char* content);

// Gửi yêu cầu tham gia nhóm
// group_name: "dev" (Hàm implement sẽ tự thêm prefix "group/")
void Net_JoinGroup(const char* group_name);


// --- 3. CHỨC NĂNG FILE ---
// Gửi file (Cắt nhỏ thành chunk và gửi)
void Net_SendFile(const char* target_id, const char* filepath);

// Yêu cầu server cho tải file về
// target_id: group/global, file_server_name: 1766_image.png
void Net_RequestDownload(const char* target_id, const char* file_server_name);

#endif