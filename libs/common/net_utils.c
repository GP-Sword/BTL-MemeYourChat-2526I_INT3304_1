#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include "net_utils.h"

int send_all(SOCKET sock, const void *buf, int len) {
    const char *p = (const char *)buf; 
    int total = 0;
    while (total < len) {
        int sent = send(sock, p + total, len - total, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return -1; //error
        }
        total += sent; // ghi đủ len byte
    }
    return total;
}

int recv_all(SOCKET sock, void *buf, int len) {
    char *p = (char*) buf;
    int total = 0;
    while (total < len) {
        int recvd = recv(sock, p+total, len - total, 0);
        if (recvd == SOCKET_ERROR || recvd == 0) {
            return -1;
        }
        total += recvd;
    }
    return total;
}

