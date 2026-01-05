#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "state.h"
#include "net_logic.h"

AppState g_State; // Khởi tạo biến state

void RenderLogin() {
    // Căn giữa màn hình
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    
    ImGui::InputText("Server IP", g_State.server_ip, 32);
    ImGui::InputInt("Port", &g_State.server_port);
    ImGui::Separator();
    ImGui::InputText("Username", g_State.username, 32);
    ImGui::InputText("Password", g_State.password, 32, ImGuiInputTextFlags_Password);
    
    if (ImGui::Button("Connect & Login", ImVec2(200, 0))) {
        if (Net_Connect(g_State.server_ip, g_State.server_port)) {
            Net_Login(g_State.username, g_State.password);
            g_State.is_logged_in = true;
        }
    }
    ImGui::End();
}

void RenderChat() {
    // Bố cục toàn màn hình
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // --- CỘT TRÁI: DANH SÁCH NHÓM ---
    ImGui::BeginChild("LeftPane", ImVec2(200, 0), true);
    ImGui::Text("Contacts / Groups");
    ImGui::Separator();
    for (auto& grp : g_State.groups) {
        if (ImGui::Selectable(grp.c_str(), g_State.current_group == grp)) {
            strcpy(g_State.current_group, grp.c_str());
            // TODO: Gửi gói tin Subscribe tới server
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- CỘT PHẢI: CHAT CONTENT ---
    ImGui::BeginGroup();
        // 1. Header
        ImGui::Text("Chatting in: %s", g_State.current_group);
        ImGui::Separator();

        // 2. Message History (Vùng cuộn)
        ImGui::BeginChild("ChatHistory", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); 
        
        // Lock mutex để đọc tin nhắn an toàn
        {
            std::lock_guard<std::mutex> lock(g_State.msg_mutex);
            for (auto& msg : g_State.messages) {
                // Chỉ hiện tin nhắn của nhóm đang chọn
                if (msg.target == std::string(g_State.current_group) || 
                   (msg.target.find("user/") == 0 && msg.sender == std::string(g_State.current_group).substr(5))) // Logic lọc đơn giản
                {
                    ImGui::TextColored(ImVec4(0,1,0,1), "[%s]:", msg.sender.c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", msg.content.c_str());
                }
            }
            // Auto scroll xuống dưới cùng khi có tin mới
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // 3. Input Area
        ImGui::Separator();
        // Cờ ImGuiInputTextFlags_EnterReturnsTrue giúp phát hiện nút Enter
        if (ImGui::InputText("Message", g_State.input_buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (strlen(g_State.input_buffer) > 0) {
                // Gửi mạng
                Net_SendMessage(g_State.current_group, g_State.input_buffer);
                
                // Hiển thị cục bộ luôn cho mượt (Optimistic UI)
                {
                    std::lock_guard<std::mutex> lock(g_State.msg_mutex);
                    g_State.messages.push_back({g_State.username, g_State.current_group, g_State.input_buffer, false});
                }
                
                // Xóa buffer
                g_State.input_buffer[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1); // Focus lại vào ô nhập
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Send")) { 
            /* Logic nút Send giống logic Enter ở trên */ 
        }

    ImGui::EndGroup();

    ImGui::End();
}

int main(int, char**) {
    // Setup Window (GLFW)
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "My Chat App", NULL, NULL);
    if (window == NULL) return 1;
    glfwMakeContextCurrent(window);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark(); // Giao diện tối
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Main Loop (Vòng lặp vĩnh cửu giống Game)
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Logic UI
        if (!g_State.is_logged_in) {
            RenderLogin();
        } else {
            RenderChat();
        }

        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}