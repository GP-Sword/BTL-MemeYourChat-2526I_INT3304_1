// Compile: gcc game_client.c -o game_client.exe -lgdi32 -luser32 -lws2_32

#define _WIN32_WINNT 0x0600
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

// --- CONFIG ---
#define SERVER_ADDRESS "127.0.0.1" // Default localhost, will override below
#define SERVER_PORT "55656"
#define BOARD_SIZE 8
#define CELL_SIZE 60  // Smaller cells for 8x8
#define BOARD_MARGIN 20
#define WINDOW_WIDTH (BOARD_SIZE * CELL_SIZE + BOARD_MARGIN * 2 + 20)
#define WINDOW_HEIGHT (BOARD_SIZE * CELL_SIZE + 100)

// --- GLOBALS ---
char board[BOARD_SIZE][BOARD_SIZE];
char playerSymbol = ' ';
char statusMessage[100] = "Connecting...";
SOCKET ConnectSocket = INVALID_SOCKET;
BOOL isMyTurn = FALSE;
BOOL isGameOver = FALSE;

// --- MESSAGES ---
#define WM_APP_UPDATE_STATE (WM_APP + 1)
#define WM_APP_GAME_OVER (WM_APP + 2)

typedef struct { char board[BOARD_SIZE][BOARD_SIZE]; char currentTurnSymbol; } GameStateUpdate;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI ReceiveThread(LPVOID lpParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TicTacToe8x8";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    CreateWindow("TicTacToe8x8", "Minigame 8x8 - Connect 4", WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInstance, NULL);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

void DrawBoard(HDC hdc) {
    for (int i = 0; i <= BOARD_SIZE; i++) {
        int pos = BOARD_MARGIN + i * CELL_SIZE;
        MoveToEx(hdc, pos, BOARD_MARGIN, NULL);
        LineTo(hdc, pos, BOARD_MARGIN + BOARD_SIZE * CELL_SIZE); // Vertical
        MoveToEx(hdc, BOARD_MARGIN, pos, NULL);
        LineTo(hdc, BOARD_MARGIN + BOARD_SIZE * CELL_SIZE, pos); // Horizontal
    }

    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (board[r][c] == ' ') continue;
            int x = BOARD_MARGIN + c * CELL_SIZE;
            int y = BOARD_MARGIN + r * CELL_SIZE;
            
            if (board[r][c] == 'X') {
                HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
                SelectObject(hdc, hPen);
                MoveToEx(hdc, x + 10, y + 10, NULL); LineTo(hdc, x + CELL_SIZE - 10, y + CELL_SIZE - 10);
                MoveToEx(hdc, x + CELL_SIZE - 10, y + 10, NULL); LineTo(hdc, x + 10, y + CELL_SIZE - 10);
                DeleteObject(hPen);
            } else {
                HPEN hPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 255));
                SelectObject(hdc, hPen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, x + 10, y + 10, x + CELL_SIZE - 10, y + CELL_SIZE - 10);
                DeleteObject(hPen);
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateThread(NULL, 0, ReceiveThread, hwnd, 0, NULL);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            TextOut(hdc, 20, WINDOW_HEIGHT - 60, statusMessage, strlen(statusMessage));
            DrawBoard(hdc);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_LBUTTONDOWN: {
            if (isGameOver || !isMyTurn) break;
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int c = (x - BOARD_MARGIN) / CELL_SIZE;
            int r = (y - BOARD_MARGIN) / CELL_SIZE;
            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE && board[r][c] == ' ') {
                char buf[32]; snprintf(buf, sizeof(buf), "MOVE;%d;%d", r, c);
                send(ConnectSocket, buf, strlen(buf), 0);
                isMyTurn = FALSE; 
                strcpy(statusMessage, "Waiting for opponent...");
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
        case WM_APP_UPDATE_STATE: {
            GameStateUpdate* u = (GameStateUpdate*)lParam;
            memcpy(board, u->board, sizeof(board));
            isMyTurn = (u->currentTurnSymbol == playerSymbol);
            if (!isGameOver) snprintf(statusMessage, sizeof(statusMessage), isMyTurn ? "Your Turn (%c)" : "Opponent's Turn", playerSymbol);
            free(u); InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case WM_APP_GAME_OVER: {
            char* txt = (char*)lParam;
            isGameOver = TRUE;
            strcpy(statusMessage, txt);
            free(txt); InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

DWORD WINAPI ReceiveThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(SERVER_PORT));
    inet_pton(AF_INET, SERVER_ADDRESS, &addr.sin_addr);
    
    if (connect(ConnectSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        strcpy(statusMessage, "Connection Failed!");
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    char buf[1024];
    while (1) {
        int n = recv(ConnectSocket, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        
        // Handle Sticky Packets (Simplified: split by newline)
        char *cmd = strtok(buf, "\n");
        while (cmd) {
            if (strncmp(cmd, "ASSIGN;", 7) == 0) {
                playerSymbol = cmd[7];
            } else if (strncmp(cmd, "UPDATE;", 7) == 0) {
                // UPDATE;BOARDSTRING;TURN
                char *boardStr = cmd + 7;
                char *turnStr = strrchr(cmd, ';');
                if (turnStr) {
                    *turnStr = 0; turnStr++;
                    GameStateUpdate* u = (GameStateUpdate*)malloc(sizeof(GameStateUpdate));
                    u->currentTurnSymbol = turnStr[0];
                    int idx = 0;
                    for(int i=0; i<BOARD_SIZE; i++) for(int j=0; j<BOARD_SIZE; j++) u->board[i][j] = boardStr[idx++];
                    PostMessage(hwnd, WM_APP_UPDATE_STATE, 0, (LPARAM)u);
                }
            } else if (strncmp(cmd, "GAMEOVER;", 9) == 0) {
                // GAMEOVER;MSG;BOARD
                char *msgStart = cmd + 9;
                char *lastSemi = strrchr(cmd, ';');
                if(lastSemi) {
                    *lastSemi = 0; 
                    // Update final board state first
                    char *boardStr = lastSemi + 1;
                    int idx = 0;
                    for(int i=0; i<BOARD_SIZE; i++) for(int j=0; j<BOARD_SIZE; j++) board[i][j] = boardStr[idx++];
                    
                    PostMessage(hwnd, WM_APP_GAME_OVER, 0, (LPARAM)_strdup(msgStart));
                }
            } else if (strncmp(cmd, "MESSAGE;", 8) == 0) {
                strcpy(statusMessage, cmd + 8);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            cmd = strtok(NULL, "\n");
        }
    }
    return 0;
}