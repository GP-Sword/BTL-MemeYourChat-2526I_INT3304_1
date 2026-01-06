// Compile: gcc game/game_client_linux.c -o game_client_linux -lglfw -lGL -lm -pthread

#include <GLFW/glfw3.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- CONFIG ---
#define SERVER_ADDRESS "127.0.0.1" 
#define SERVER_PORT 55656
#define BOARD_SIZE 8
#define CELL_SIZE 60
#define BOARD_MARGIN 20
#define WINDOW_WIDTH (BOARD_SIZE * CELL_SIZE + BOARD_MARGIN * 2)
#define WINDOW_HEIGHT (BOARD_SIZE * CELL_SIZE + 50)

// --- GLOBALS ---
char board[BOARD_SIZE][BOARD_SIZE];
char playerSymbol = ' ';
char statusMessage[256] = "Connecting...";
int isMyTurn = 0;
int isGameOver = 0;
int sock = -1;

// --- DRAWING HELPERS ---
void draw_circle(float cx, float cy, float r, int segments) {
    glBegin(GL_LINE_LOOP);
    for(int i = 0; i < segments; i++) {
        float theta = 2.0f * 3.1415926f * (float)i / (float)segments;
        float x = r * cosf(theta);
        float y = r * sinf(theta);
        glVertex2f(cx + x, cy + y);
    }
    glEnd();
}

void draw_x(float cx, float cy, float r) {
    glBegin(GL_LINES);
    glVertex2f(cx - r, cy - r); glVertex2f(cx + r, cy + r);
    glVertex2f(cx + r, cy - r); glVertex2f(cx - r, cy + r);
    glEnd();
}

// --- NETWORK THREAD ---
void* network_thread(void* arg) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    // Lưu ý: Nếu server chạy máy khác, hãy đổi IP này
    if(inet_pton(AF_INET, SERVER_ADDRESS, &serv_addr.sin_addr) <= 0) {
        strcpy(statusMessage, "Invalid Address");
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        strcpy(statusMessage, "Connection Failed! Is server running?");
        return NULL;
    }

    char buffer[1024];
    while (1) {
        int n = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        
        char *cmd = strtok(buffer, "\n");
        while (cmd) {
            if (strncmp(cmd, "ASSIGN;", 7) == 0) {
                playerSymbol = cmd[7];
            } 
            else if (strncmp(cmd, "UPDATE;", 7) == 0) {
                // UPDATE;BOARDSTRING;TURN
                char *boardStr = cmd + 7;
                char *turnStr = strrchr(cmd, ';');
                if (turnStr) {
                    *turnStr = 0; turnStr++;
                    // Update Board
                    int idx = 0;
                    for(int i=0; i<BOARD_SIZE; i++) 
                        for(int j=0; j<BOARD_SIZE; j++) 
                            board[i][j] = boardStr[idx++];
                    
                    // Update Turn
                    isMyTurn = (turnStr[0] == playerSymbol);
                    
                    if (!isGameOver) {
                        snprintf(statusMessage, sizeof(statusMessage), 
                            "You are %c. %s", 
                            playerSymbol, 
                            isMyTurn ? "YOUR TURN!" : "Opponent's turn...");
                    }
                }
            } 
            else if (strncmp(cmd, "GAMEOVER;", 9) == 0) {
                char *msgStart = cmd + 9;
                char *lastSemi = strrchr(cmd, ';');
                if(lastSemi) {
                    *lastSemi = 0; 
                    char *boardStr = lastSemi + 1;
                    int idx = 0; 
                    for(int i=0; i<BOARD_SIZE; i++) 
                        for(int j=0; j<BOARD_SIZE; j++) 
                            board[i][j] = boardStr[idx++];
                    
                    isGameOver = 1;
                    snprintf(statusMessage, sizeof(statusMessage), "GAME OVER: %s", msgStart);
                }
            } 
            else if (strncmp(cmd, "MESSAGE;", 8) == 0) {
                strcpy(statusMessage, cmd + 8);
            }
            cmd = strtok(NULL, "\n");
        }
    }
    return NULL;
}

// --- INPUT HANDLING ---
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (isGameOver || !isMyTurn) return;
        
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        
        int c = (int)(xpos - BOARD_MARGIN) / CELL_SIZE;
        int r = (int)(ypos - BOARD_MARGIN) / CELL_SIZE;
        
        if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE && board[r][c] == ' ') {
            char buf[32];
            snprintf(buf, sizeof(buf), "MOVE;%d;%d", r, c);
            send(sock, buf, strlen(buf), 0);
            
            isMyTurn = 0;
            strcpy(statusMessage, "Sending move...");
        }
    }
}

// --- MAIN ---
int main(void) {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "TicTacToe 8x8 (Linux Client)", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // Setup 2D projection (Origin at top-left)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    
    // Start Network Thread
    pthread_t tid;
    pthread_create(&tid, NULL, network_thread, NULL);

    while (!glfwWindowShouldClose(window)) {
        // Clear screen (White)
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT);

        // Update Title with Status
        glfwSetWindowTitle(window, statusMessage);

        // Draw Grid
        glColor3f(0.0f, 0.0f, 0.0f); // Black
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        for (int i = 0; i <= BOARD_SIZE; i++) {
            float pos = (float)(BOARD_MARGIN + i * CELL_SIZE);
            // Vertical
            glVertex2f(pos, (float)BOARD_MARGIN); 
            glVertex2f(pos, (float)(BOARD_MARGIN + BOARD_SIZE * CELL_SIZE)); 
            // Horizontal
            glVertex2f((float)BOARD_MARGIN, pos); 
            glVertex2f((float)(BOARD_MARGIN + BOARD_SIZE * CELL_SIZE), pos); 
        }
        glEnd();

        // Draw Pieces
        for (int r = 0; r < BOARD_SIZE; r++) {
            for (int c = 0; c < BOARD_SIZE; c++) {
                if (board[r][c] == ' ') continue;
                
                float cx = (float)(BOARD_MARGIN + c * CELL_SIZE + CELL_SIZE/2);
                float cy = (float)(BOARD_MARGIN + r * CELL_SIZE + CELL_SIZE/2);
                float rad = (float)(CELL_SIZE/2 - 8);
                
                glLineWidth(3.0f);
                if (board[r][c] == 'X') {
                    glColor3f(0.8f, 0.0f, 0.0f); // Red
                    draw_x(cx, cy, rad);
                } else {
                    glColor3f(0.0f, 0.0f, 0.8f); // Blue
                    draw_circle(cx, cy, rad, 32);
                }
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (sock != -1) close(sock);
    glfwTerminate();
    return 0;
}