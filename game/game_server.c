/**
 * @file game_server.c
 * @brief Cross-platform Game Server (Windows/Linux)
 * Logic: 8x8 Board, Win condition: 4 in a row.
 * Compile Linux: gcc game_server.c -o game_server -lpthread
 * Compile Windows: gcc game_server.c -o game_server.exe -lws2_32
 */

#if defined(_WIN32)
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    #define THREAD_RET DWORD WINAPI
    #define THREAD_ARG LPVOID
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <string.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <errno.h>
    typedef int socket_t;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define CLOSE_SOCKET close
    #define THREAD_RET void*
    #define THREAD_ARG void*
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 55656
#define BOARD_SIZE 8
#define WIN_LEN 4

typedef struct {
    socket_t playerX;
    socket_t playerO;
    int sessionId;
} GAME_SESSION;

volatile int sessionIdCounter = 0;

void SendCommand(socket_t s, const char* command) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s\n", command);
    send(s, buffer, (int)strlen(buffer), 0);
}

// Logic check win 4 ô thẳng hàng cho bàn cờ 8x8
int CheckWin(char board[BOARD_SIZE][BOARD_SIZE], char p) {
    // Ngang, Dọc, Chéo chính, Chéo phụ
    int directions[4][2] = {{0,1}, {1,0}, {1,1}, {1,-1}};
    
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (board[r][c] != p) continue;

            for (int d = 0; d < 4; d++) {
                int count = 0;
                for (int k = 0; k < WIN_LEN; k++) {
                    int nr = r + k * directions[d][0];
                    int nc = c + k * directions[d][1];
                    if (nr >= 0 && nr < BOARD_SIZE && nc >= 0 && nc < BOARD_SIZE && board[nr][nc] == p) {
                        count++;
                    } else {
                        break;
                    }
                }
                if (count == WIN_LEN) return 1;
            }
        }
    }
    return 0;
}

int CheckDraw(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) 
        for (int j = 0; j < BOARD_SIZE; j++) 
            if (board[i][j] == ' ') return 0;
    return 1;
}

THREAD_RET HandleGame(THREAD_ARG lpParam) {
    GAME_SESSION* session = (GAME_SESSION*)lpParam;
    socket_t playerX = session->playerX;
    socket_t playerO = session->playerO;
    int sessionId = session->sessionId;
    char recvbuf[512];
    char sendbuf[512];

    printf("[SESSION %d] Started.\n", sessionId);
    
    int sessionActive = 1;
    while(sessionActive) {
        SendCommand(playerX, "ASSIGN;X");
        SendCommand(playerO, "ASSIGN;O");

        char board[BOARD_SIZE][BOARD_SIZE];
        memset(board, ' ', sizeof(board));
        
        socket_t currentPlayerSocket = playerX;
        char currentPlayerSymbol = 'X';
        int gameRunning = 1;

        while (gameRunning) {
            // Serialize board (64 chars)
            char boardStr[BOARD_SIZE*BOARD_SIZE + 1];
            int idx = 0;
            for(int i=0; i<BOARD_SIZE; i++) 
                for(int j=0; j<BOARD_SIZE; j++) 
                    boardStr[idx++] = board[i][j];
            boardStr[idx] = '\0';
            
            snprintf(sendbuf, sizeof(sendbuf), "UPDATE;%s;%c", boardStr, currentPlayerSymbol);
            SendCommand(playerX, sendbuf);
            SendCommand(playerO, sendbuf);

            int iResult = recv(currentPlayerSocket, recvbuf, sizeof(recvbuf) - 1, 0);
            if (iResult <= 0) {
                socket_t other = (currentPlayerSocket == playerX) ? playerO : playerX;
                SendCommand(other, "OPPONENT_QUIT");
                sessionActive = 0;
                break;
            }
            recvbuf[iResult] = '\0';
            
            int row, col;
            if (sscanf(recvbuf, "MOVE;%d;%d", &row, &col) == 2) {
                if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE && board[row][col] == ' ') {
                    board[row][col] = currentPlayerSymbol;

                    if (CheckWin(board, currentPlayerSymbol)) {
                        // Re-serialize for final update
                        idx=0;
                        for(int i=0; i<BOARD_SIZE; i++) for(int j=0; j<BOARD_SIZE; j++) boardStr[idx++] = board[i][j];
                        boardStr[idx] = '\0';

                        snprintf(sendbuf, sizeof(sendbuf), "GAMEOVER;Player %c Wins!;%s", currentPlayerSymbol, boardStr);
                        SendCommand(playerX, sendbuf);
                        SendCommand(playerO, sendbuf);
                        gameRunning = 0;
                    } else if (CheckDraw(board)) {
                        // Re-serialize
                        idx=0;
                        for(int i=0; i<BOARD_SIZE; i++) for(int j=0; j<BOARD_SIZE; j++) boardStr[idx++] = board[i][j];
                        boardStr[idx] = '\0';

                        snprintf(sendbuf, sizeof(sendbuf), "GAMEOVER;It's a Draw!;%s", boardStr);
                        SendCommand(playerX, sendbuf);
                        SendCommand(playerO, sendbuf);
                        gameRunning = 0;
                    } else {
                        currentPlayerSymbol = (currentPlayerSymbol == 'X') ? 'O' : 'X';
                        currentPlayerSocket = (currentPlayerSocket == playerX) ? playerO : playerX;
                    }
                }
            }
        } 

        if(!sessionActive) break;

        // Simple Rematch: Just wait for 5 seconds then restart or quit (Simplified)
        // For production, implement the handshake again.
        // Here we just loop back if no one quit.
        // Wait for QUIT or REMATCH from both... (Skipped for brevity in this snippet)
        // Assuming auto-restart or client disconnects manually.
        sessionActive = 0; // End session after one game for simplicity in this version
    }

    CLOSE_SOCKET(playerX);
    CLOSE_SOCKET(playerO);
    free(session);
    printf("[SESSION %d] Ended.\n", sessionId);
    return 0;
}

int main() {
#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socket_t ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(DEFAULT_PORT);

    if (bind(ListenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("Bind failed.\n"); return 1;
    }
    listen(ListenSocket, 5);
    printf("Game Server 8x8 running on port %d...\n", DEFAULT_PORT);

    socket_t waitingPlayer = INVALID_SOCKET;

    while (1) {
        socket_t client = accept(ListenSocket, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        printf("New client connected.\n");

        if (waitingPlayer == INVALID_SOCKET) {
            waitingPlayer = client;
            SendCommand(waitingPlayer, "MESSAGE;Waiting for opponent...");
        } else {
            printf("Pairing players...\n");
            GAME_SESSION *session = (GAME_SESSION*)malloc(sizeof(GAME_SESSION));
            session->playerX = waitingPlayer;
            session->playerO = client;
            session->sessionId = sessionIdCounter++;

#if defined(_WIN32)
            CreateThread(NULL, 0, HandleGame, session, 0, NULL);
#else
            pthread_t thread_id;
            pthread_create(&thread_id, NULL, HandleGame, session);
            pthread_detach(thread_id);
#endif
            waitingPlayer = INVALID_SOCKET;
        }
    }
    return 0;
}