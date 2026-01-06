// client/main.cpp - CODE ĐẦY ĐỦ
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "state.h"
#include "net_logic.h"

// Biến toàn cục lưu trạng thái ứng dụng
AppState g_State; 

// --- GIAO DIỆN ĐĂNG NHẬP ---
void RenderLogin() {
    // Căn giữa màn hình
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    
    ImGui::Text("WELCOME TO CHAT");
    ImGui::Separator();
    
    ImGui::InputText("Server IP", g_State.server_ip, 32);
    ImGui::InputInt("Port", &g_State.server_port);
    ImGui::Separator();
    ImGui::InputText("Username", g_State.username, 32);
    ImGui::InputText("Password", g_State.password, 32, ImGuiInputTextFlags_Password);
    
    if (ImGui::Button("Connect & Login", ImVec2(250, 0))) {
        if (Net_Connect(g_State.server_ip, g_State.server_port)) {
            // Gửi gói tin login
            Net_Login(g_State.username, g_State.password);
            
            // Tạm thời set true để chuyển màn hình, logic chuẩn sẽ đợi Server phản hồi OK mới set
            g_State.is_logged_in = true; 
        }
    }
    ImGui::End();
}

// --- GIAO DIỆN CHAT CHÍNH ---
void RenderChat() {
    // Cài đặt khung hình full màn hình
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // --- CỘT TRÁI: DANH SÁCH (SIDEBAR) ---
    ImGui::BeginChild("LeftPane", ImVec2(250, 0), true);
    
<<<<<<< HEAD
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
=======
    ImGui::Text("User: %s", g_State.username);
    ImGui::Separator();

    // Ô thêm nhóm / Chat riêng
    ImGui::InputTextWithHint("##Add", "Join Group / PM User...", g_State.search_buffer, 64);
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        std::string input = g_State.search_buffer;
        if (!input.empty()) {
            if (input[0] == '@') { 
                // Chat riêng (VD: @Bob)
                std::string user = input.substr(1);
                std::string topic = "user/" + user;
                {
                    std::lock_guard<std::mutex> lock(g_State.data_mutex);
                    if (g_State.conversations.find(topic) == g_State.conversations.end()) {
                        g_State.conversations[topic] = {topic, user, {}, false};
                    }
                    g_State.current_chat_id = topic;
                }
            } else {
                // Join Group (VD: devteam) -> Gọi hàm Net_JoinGroup
                Net_JoinGroup(input.c_str());
            }
            g_State.search_buffer[0] = '\0';
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
        }
    }

    ImGui::Separator();
<<<<<<< HEAD
    
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
=======

    // Render danh sách các cuộc hội thoại
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
    {
        std::lock_guard<std::mutex> lock(g_State.data_mutex);
        for (auto& [id, conv] : g_State.conversations) {
            bool is_selected = (g_State.current_chat_id == id);
            
<<<<<<< HEAD
            // Đặt màu khác nhau cho Group và User để dễ phân biệt
            if (id.find("group/") == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f)); // Xanh nhạt cho Group
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f)); // Xanh lá cho User
            }

=======
            // Màu sắc khác nhau cho Group và User
            ImVec4 color = (id.find("group/") == 0) ? ImVec4(0.8f, 0.8f, 1.0f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
            if (ImGui::Selectable(conv.name.c_str(), is_selected)) {
                g_State.current_chat_id = id;
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

<<<<<<< HEAD
    // --- CHAT BOX (Phần bên phải giữ nguyên) ---
=======
    // --- CỘT PHẢI: KHUNG CHAT ---
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
    ImGui::BeginGroup();
        // Lấy Conversation hiện tại
        Conversation* current_conv = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_State.data_mutex);
            if (g_State.conversations.find(g_State.current_chat_id) != g_State.conversations.end()) {
                current_conv = &g_State.conversations[g_State.current_chat_id];
            }
        }

        if (current_conv) {
            // 1. Header
            ImGui::Text("Chatting in: %s", current_conv->name.c_str());
            ImGui::Separator();

            // 2. Nội dung tin nhắn
            ImGui::BeginChild("ChatHistory", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); 
            {
                std::lock_guard<std::mutex> lock(g_State.data_mutex);
                for (auto& msg : current_conv->messages) {
<<<<<<< HEAD
                    ImGui::TextDisabled("[%lld]", msg.timestamp); ImGui::SameLine();
                    
                    // Highlight người gửi
                    if (msg.sender == std::string(g_State.username)) {
                        ImGui::TextColored(ImVec4(0,1,1,1), "Me:"); 
                    } else {
                        ImGui::TextColored(ImVec4(1,1,0,1), "%s:", msg.sender.c_str());
                    }
                    ImGui::SameLine();
=======
                    // Màu sắc người gửi
                    if (msg.sender == std::string(g_State.username)) {
                        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Me:");
                    } else {
                        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s:", msg.sender.c_str());
                    }
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
                    
                    ImGui::SameLine();
                    if (msg.is_file) {
                        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "[FILE] %s", msg.content.c_str());
                    } else {
                        ImGui::TextWrapped("%s", msg.content.c_str());
                    }
                }
                
<<<<<<< HEAD
                // Auto scroll xuống dưới cùng nếu đang ở dưới
=======
                // Auto scroll
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            // 3. Ô nhập liệu
            ImGui::Separator();
            if (ImGui::InputText("Message", g_State.input_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(g_State.input_buffer) > 0) {
                    // Gửi tin nhắn qua mạng
                    Net_SendMessage(current_conv->id.c_str(), g_State.input_buffer);
                    
                    // Hiển thị cục bộ luôn cho mượt (Optimistic UI)
                    {
                        std::lock_guard<std::mutex> lock(g_State.data_mutex);
                        Message m = { (long long)time(NULL), std::string(g_State.username), std::string(g_State.input_buffer), false, false };
                        g_State.conversations[g_State.current_chat_id].messages.push_back(m);
                    }
                    
                    g_State.input_buffer[0] = '\0';
                    ImGui::SetKeyboardFocusHere(-1); // Focus lại vào ô nhập
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Send")) { 
                // Logic nút Send (giống Enter)
                if (strlen(g_State.input_buffer) > 0) {
                    Net_SendMessage(current_conv->id.c_str(), g_State.input_buffer);
                    {
                        std::lock_guard<std::mutex> lock(g_State.data_mutex);
                        Message m = { (long long)time(NULL), std::string(g_State.username), std::string(g_State.input_buffer), false, false };
                        g_State.conversations[g_State.current_chat_id].messages.push_back(m);
                    }
                    g_State.input_buffer[0] = '\0';
                    ImGui::SetKeyboardFocusHere(-1);
                }
            }
<<<<<<< HEAD

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
        } 
=======
        } else {
            ImGui::Text("Select a conversation or join a group to start chatting.");
        }
>>>>>>> parent of a8797bf (Add Real-time Chat function (Still wrong the Add Group/User Func))

    ImGui::EndGroup();
    ImGui::End();
}

// --- HÀM MAIN (QUAN TRỌNG NHẤT) ---
int main(int argc, char** argv) {
    // 1. Khởi tạo GLFW
    if (!glfwInit()) return 1;
    
    // Tạo cửa sổ
    GLFWwindow* window = glfwCreateWindow(1280, 720, "My Chat App", NULL, NULL);
    if (window == NULL) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Bật V-Sync

    // 2. Khởi tạo ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark(); // Giao diện tối
    
    // Setup Backend cho ImGui
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 3. Vòng lặp chính (Game Loop)
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start frame mới
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Logic điều hướng màn hình
        if (!g_State.is_logged_in) {
            RenderLogin();
        } else {
            RenderChat();
        }

        // Render ra màn hình
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.00f); // Màu nền
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window);
    }

    // 4. Dọn dẹp khi tắt app
    Net_Disconnect(); // Ngắt kết nối socket
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}