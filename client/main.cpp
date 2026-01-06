#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h> // Để dùng File Dialog
#include "state.h"
#include "net_logic.h"
#include "../libs/common/protocol.h"
#include <shellapi.h>

AppState g_State; 

// Hàm mở File Dialog của Windows
bool OpenFileDialog(char* buffer, int max_len) {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        strncpy(buffer, ofn.lpstrFile, max_len);
        return true;
    }
    return false;
}

void RenderLogin() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::InputText("Server IP", g_State.server_ip, 32);
    ImGui::InputInt("Port", &g_State.server_port);
    ImGui::InputText("Username", g_State.username, 32);
    ImGui::InputText("Password", g_State.password, 32, ImGuiInputTextFlags_Password);
    if (ImGui::Button("Login", ImVec2(200,0))) {
        if(Net_Connect(g_State.server_ip, g_State.server_port)) {
            Net_Login(g_State.username, g_State.password);
            g_State.is_logged_in = true;
        }
    }
    ImGui::End();
}

void RenderChat() {
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // --- SIDEBAR ---
    ImGui::BeginChild("LeftPane", ImVec2(250, 0), true);
    ImGui::Text("User: %s", g_State.username);
    
    // Hiển thị trạng thái Download (Giữ nguyên)
    {
        std::lock_guard<std::mutex> lock(g_State.data_mutex);
        if (g_State.download_state.is_downloading) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0,1,0,1), "Downloading...");
            float progress = 0.0f;
            if (g_State.download_state.total_size > 0)
                progress = (float)g_State.download_state.received_size / g_State.download_state.total_size;
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
        }
    }

    ImGui::Separator();
    
    // --- 1. JOIN GROUP ---
    ImGui::TextDisabled("Groups");
    ImGui::InputTextWithHint("##JoinGrp", "Group Name...", g_State.search_buffer, 64);
    ImGui::SameLine();
    if (ImGui::Button("Join##Grp")) {
        if (strlen(g_State.search_buffer) > 0) {
            Net_JoinGroup(g_State.search_buffer);
            g_State.search_buffer[0] = '\0';
        }
    }

    // --- 2. ADD USER (CHAT RIÊNG) ---
    ImGui::Spacing();
    ImGui::TextDisabled("Direct Messages");
    ImGui::InputTextWithHint("##AddPM", "Username...", g_State.pm_search_buffer, 64);
    ImGui::SameLine();
    if (ImGui::Button("Chat##PM")) {
        if (strlen(g_State.pm_search_buffer) > 0) {
            std::string user = g_State.pm_search_buffer;
            
            // Không tự chat với chính mình
            if (user != std::string(g_State.username)) {
                std::string topic = "user/" + user;
                
                std::lock_guard<std::mutex> lock(g_State.data_mutex);
                if (g_State.conversations.find(topic) == g_State.conversations.end()) {
                    // Tạo hội thoại local ngay lập tức
                    Conversation new_conv;
                    new_conv.id = topic;
                    new_conv.name = user; // Tên hiển thị là tên user
                    g_State.conversations[topic] = new_conv;
                }
                // Chuyển sang khung chat này
                g_State.current_chat_id = topic;
                
                // Clear ô nhập
                g_State.pm_search_buffer[0] = '\0';
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Conversations");
    
    // --- LIST CONVERSATIONS ---
    {
        std::lock_guard<std::mutex> lock(g_State.data_mutex);
        for (auto& [id, conv] : g_State.conversations) {
            bool is_selected = (g_State.current_chat_id == id);
            
            // Đặt màu khác nhau cho Group và User để dễ phân biệt
            if (id.find("group/") == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f)); // Xanh nhạt cho Group
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f)); // Xanh lá cho User
            }

            if (ImGui::Selectable(conv.name.c_str(), is_selected)) {
                g_State.current_chat_id = id;
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- CHAT BOX (Phần bên phải giữ nguyên) ---
    ImGui::BeginGroup();
        Conversation* current_conv = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            if (g_State.conversations.count(g_State.current_chat_id))
                current_conv = &g_State.conversations[g_State.current_chat_id];
        }

        if (current_conv) {
            ImGui::Text("Chat: %s", current_conv->name.c_str());
            ImGui::Separator();
            
            // --- VÙNG HIỂN THỊ LỊCH SỬ CHAT  ---
            ImGui::BeginChild("History", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
            {
                std::lock_guard<std::mutex> lock(g_State.data_mutex);
                for (auto& msg : current_conv->messages) {
                    ImGui::TextDisabled("[%lld]", msg.timestamp); ImGui::SameLine();
                    
                    // Highlight người gửi
                    if (msg.sender == std::string(g_State.username)) {
                        ImGui::TextColored(ImVec4(0,1,1,1), "Me:"); 
                    } else {
                        ImGui::TextColored(ImVec4(1,1,0,1), "%s:", msg.sender.c_str());
                    }
                    ImGui::SameLine();
                    
                    if (msg.is_file) {
                        std::string label = "[FILE] " + msg.file_name + " (Click to Download)";
                        if (ImGui::SmallButton(label.c_str())) {
                            Net_RequestDownload(current_conv->id.c_str(), msg.download_id.c_str());
                        }
                    } else {
                        ImGui::TextWrapped("%s", msg.content.c_str());
                    }
                }
                
                // Auto scroll xuống dưới cùng nếu đang ở dưới
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::Separator();
            // Input Area
            // 1. Nút Attach
            if (ImGui::Button("Attach...")) {
                char filepath[260] = {0};
                if (OpenFileDialog(filepath, 260)) {
                    Net_SendFile(current_conv->id.c_str(), filepath);
                    std::lock_guard<std::mutex> lock(g_State.data_mutex);
                    Message m; m.timestamp = time(NULL); m.sender = g_State.username;
                    m.content = "Sending file: " + std::string(filepath);
                    m.is_file = false; m.file_name = ""; m.download_id = ""; m.is_history = false;
                    current_conv->messages.push_back(m);
                }
            }
            
            ImGui::SameLine();
            
            // 2. Ô Nhập Text (Input)
            // Tính toán chiều rộng để chừa chỗ cho 2 nút Send và Game
            float width = ImGui::GetContentRegionAvail().x - 110; // Trừ đi khoảng 110 pixel cho 2 nút bên phải
            ImGui::PushItemWidth(width);
            
            bool enter_pressed = ImGui::InputText("##Msg", g_State.input_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
            
            ImGui::PopItemWidth();
            ImGui::SameLine();
            
            // 3. Nút Send
            if (ImGui::Button("Send") || enter_pressed) {
                if (strlen(g_State.input_buffer) > 0) {
                    Net_SendMessage(current_conv->id.c_str(), g_State.input_buffer);
                    
                    // Hiện ngay lên màn hình
                    std::lock_guard<std::mutex> lock(g_State.data_mutex);
                    Message m; m.timestamp = time(NULL); m.sender = g_State.username;
                    m.content = g_State.input_buffer;
                    m.is_file = false; m.file_name = ""; m.download_id = ""; m.is_history = false;
                    current_conv->messages.push_back(m);

                    g_State.input_buffer[0] = '\0';
                    ImGui::SetKeyboardFocusHere(-1); // Focus lại vào ô chat
                }
            }

            // 4. Nút Game (ĐẶT CẠNH NÚT SEND)
            ImGui::SameLine();
            // Tô màu cam cho nổi bật
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f)); 
            if (ImGui::Button("X0")) {
                // Mở game client
                ShellExecuteA(NULL, "open", "game_client.exe", NULL, NULL, SW_SHOW);
            }
            ImGui::PopStyleColor();
            // ----------------------------------------
        } 
        
    ImGui::EndGroup();
    ImGui::End();
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1000, 600, "Chat", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!g_State.is_logged_in) {
            RenderLogin();
        } else {
            RenderChat();
        }

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0,0,w,h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    
    printf("[DEBUG] Client PacketHeader size: %llu bytes (Must be 69)\n", sizeof(PacketHeader));

    Net_Disconnect();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}