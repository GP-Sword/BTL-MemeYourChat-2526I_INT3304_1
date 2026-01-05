#ifndef NET_LOGIC_H
#define NET_LOGIC_H

// Khai báo các hàm để main.cpp có thể gọi
bool Net_Connect(const char* ip, int port);
void Net_Login(const char* user, const char* pass);
void Net_SendMessage(const char* target, const char* content);

#endif