#ifndef NET_UTILS_H
#define NET_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>

int send_all(SOCKET sock, const void *buf, int len);
int recv_all(SOCKET sock, void *buf, int len);

#ifdef __cplusplus
}
#endif


#endif