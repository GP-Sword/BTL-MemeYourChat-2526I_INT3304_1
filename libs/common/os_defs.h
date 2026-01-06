#ifndef OS_DEFS_H
#define OS_DEFS_H

#ifdef _WIN32
    // --- Windows ---
    #define _CRT_SECURE_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <process.h>
    #include <direct.h>

#else
    // --- Linux ---
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <stdlib.h>
    #include <string.h>

    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1

    #define closesocket(s) close(s)
    
    #define CRITICAL_SECTION pthread_mutex_t
    #define InitializeCriticalSection(cs) pthread_mutex_init(cs, NULL)
    #define DeleteCriticalSection(cs) pthread_mutex_destroy(cs)
    #define EnterCriticalSection(cs) pthread_mutex_lock(cs)
    #define LeaveCriticalSection(cs) pthread_mutex_unlock(cs)

    #define _mkdir(path) mkdir(path, 0777) 
    
    #define _strtoui64 strtoull
    #define _strtoi64 strtoll
    #define _stricmp strcasecmp

    #define WSADATA int
    #define WSAStartup(ver, data) 0
    #define WSACleanup() 0
#endif

#endif