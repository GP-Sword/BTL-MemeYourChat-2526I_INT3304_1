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
    #include <sys/stat.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <ctype.h>

    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define TRUE 1
    #define FALSE 0

    typedef unsigned long DWORD;
    typedef void* LPVOID;
    typedef void* HANDLE;

    #define closesocket(s) close(s)

    typedef int WSADATA;
    #define WSAStartup(ver, data) 0
    #define WSACleanup() ((void)0)
    #define MAKEWORD(a, b) 0

    typedef pthread_mutex_t CRITICAL_SECTION;
    
    #define InitializeCriticalSection(cs) pthread_mutex_init(cs, NULL)
    #define DeleteCriticalSection(cs) pthread_mutex_destroy(cs)
    #define EnterCriticalSection(cs) pthread_mutex_lock(cs)
    #define LeaveCriticalSection(cs) pthread_mutex_unlock(cs)

    #define _mkdir(path) mkdir(path, 0777)
    #define _strtoui64 strtoull
    #define _strtoi64 strtoll
    #define _stricmp strcasecmp

    #define Sleep(ms) usleep((ms) * 1000)

#endif

#endif