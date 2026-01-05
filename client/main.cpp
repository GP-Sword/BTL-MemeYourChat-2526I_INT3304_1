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
    
    // Hiển thị trạng thái Download
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
    ImGui::InputTextWithHint("##Add", "Join Group...", g_State.search_buffer, 64);
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        if (strlen(g_State.search_buffer) > 0) Net_JoinGroup(g_State.search_buffer);
    }

    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(g_State.data_mutex);
        for (auto& [id, conv] : g_State.conversations) {
            bool is_selected = (g_State.current_chat_id == id);
            if (ImGui::Selectable(conv.name.c_str(), is_selected)) g_State.current_chat_id = id;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- CHAT BOX ---
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

            ImGui::BeginChild("History", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
            {
                std::lock_guard<std::mutex> lock(g_State.data_mutex);
                for (auto& msg : current_conv->messages) {
                    ImGui::TextDisabled("[%lld]", msg.timestamp); ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1,1,0,1), "%s:", msg.sender.c_str()); ImGui::SameLine();
                    
                    if (msg.is_file) {
                        std::string label = "[FILE] " + msg.file_name + " (Click to Download)";
                        if (ImGui::SmallButton(label.c_str())) {
                            Net_RequestDownload(current_conv->id.c_str(), msg.download_id.c_str());
                        }
                    } else {
                        ImGui::TextWrapped("%s", msg.content.c_str());
                    }
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            // Input Area
            if (ImGui::Button("Attach...")) {
                char filepath[260] = {0};
                if (OpenFileDialog(filepath, 260)) {
                    Net_SendFile(current_conv->id.c_str(), filepath);
                    
                    std::lock_guard<std::mutex> lock(g_State.data_mutex);
                    Message m;
                    m.timestamp = time(NULL);
                    m.sender = g_State.username;
                    m.content = "Sending file: " + std::string(filepath);
                    m.is_file = false; 
                    m.file_name = "";
                    m.download_id = "";
                    m.is_history = false;
                    current_conv->messages.push_back(m);
                }
            }
            ImGui::SameLine();
            
            // Xử lý khi ấn Enter
            if (ImGui::InputText("##Msg", g_State.input_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(g_State.input_buffer) > 0) {
                    Net_SendMessage(current_conv->id.c_str(), g_State.input_buffer);
                    
                    // HIỂN THỊ NGAY
                    {
                        std::lock_guard<std::mutex> lock(g_State.data_mutex);
                        Message m;
                        m.timestamp = time(NULL);
                        m.sender = g_State.username;
                        m.content = g_State.input_buffer;
                        m.is_file = false;
                        m.file_name = "";
                        m.download_id = "";
                        m.is_history = false;
                        current_conv->messages.push_back(m);
                    }

                    g_State.input_buffer[0] = '\0';
                    ImGui::SetKeyboardFocusHere(-1);
                }
            }

            ImGui::SameLine();
            
            // Xử lý khi ấn nút Send
            if (ImGui::Button("Send")) {
                if (strlen(g_State.input_buffer) > 0) {
                    Net_SendMessage(current_conv->id.c_str(), g_State.input_buffer);

                    // HIỂN THỊ NGAY
                    {
                        std::lock_guard<std::mutex> lock(g_State.data_mutex);
                        Message m;
                        m.timestamp = time(NULL);
                        m.sender = g_State.username;
                        m.content = g_State.input_buffer;
                        m.is_file = false;
                        m.file_name = "";
                        m.download_id = "";
                        m.is_history = false;
                        current_conv->messages.push_back(m);
                    }
                    g_State.input_buffer[0] = '\0';
                }
            }
        } // <--- ĐÃ THÊM DẤU NGOẶC ĐÓNG NÀY (Đây là chỗ gây lỗi)

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