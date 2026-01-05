cmake_minimum_required(VERSION 3.10)
project(ChatClient)

set(CMAKE_CXX_STANDARD 17)

# --- [QUAN TRỌNG] Link tĩnh để không bị lỗi thiếu DLL ---
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
# ------------------------------------------------------

# 1. Cấu hình ImGui
set(IMGUI_DIR "libs/imgui")
file(GLOB IMGUI_SOURCES 
    "${IMGUI_DIR}/*.cpp"
    "${IMGUI_DIR}/*.h"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# 2. Cấu hình GLFW
set(GLFW_DIR "libs/glfw")
include_directories("${GLFW_DIR}/include")
link_directories("${GLFW_DIR}/lib-mingw-w64") 

# 3. Tạo file chạy (Executable)
# Lưu ý: Không thêm client.c vào đây để tránh trùng hàm main()
add_executable(ChatClient 
    client/main.cpp 
    client/net_logic.cpp 
    libs/common/net_utils.c
    ${IMGUI_SOURCES}
)

# 4. Include các thư mục Header
target_include_directories(ChatClient PRIVATE 
    client
    libs/common
    libs/imgui
    libs/imgui/backends
    libs/glfw/include
)

# 5. Link thư viện
target_link_libraries(ChatClient PRIVATE 
    glfw3 
    opengl32 
    ws2_32 
    imm32
    gdi32
)