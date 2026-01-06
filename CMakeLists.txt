cmake_minimum_required(VERSION 3.10)
project(MemeYourChat)

set(CMAKE_CXX_STANDARD 17)

# ==========================================
# 1. CẤU HÌNH CHUNG
# ==========================================
set(IMGUI_DIR "libs/imgui")
file(GLOB IMGUI_SOURCES 
    "${IMGUI_DIR}/*.cpp"
    "${IMGUI_DIR}/*.h"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# Tạo file thực thi
add_executable(ChatClient 
    client/main.cpp 
    client/net_logic.cpp 
    libs/common/net_utils.c
    ${IMGUI_SOURCES}
)

target_include_directories(ChatClient PRIVATE 
    ${CMAKE_SOURCE_DIR}/client
    ${CMAKE_SOURCE_DIR}/libs/common
    ${CMAKE_SOURCE_DIR}/libs/imgui
    ${CMAKE_SOURCE_DIR}/libs/imgui/backends
)

# ==========================================
# 2. CẤU HÌNH RIÊNG CHO WINDOWS (Sửa lỗi Linker)
# ==========================================
if(WIN32)
    message(STATUS "Windows Detected - Linking Static GLFW")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")

    # Include Header
    target_include_directories(ChatClient PRIVATE libs/glfw/include)

    # --- SỬA LỖI Ở ĐÂY: Trỏ thẳng vào file .a ---
    # Thay vì bắt CMake tìm, ta đưa đường dẫn cụ thể luôn
    set(GLFW_LIB "${CMAKE_SOURCE_DIR}/libs/glfw/lib-mingw-w64/libglfw3.a")

    target_link_libraries(ChatClient PRIVATE 
        ${GLFW_LIB}  # <--- Link file trực tiếp
        opengl32 
        ws2_32 
        imm32
        gdi32
        comdlg32
        "-mconsole"
    )

elseif(UNIX AND NOT APPLE)
    # Cấu hình Linux (Giữ nguyên)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
    find_package(OpenGL REQUIRED)
    target_include_directories(ChatClient PRIVATE ${GLFW3_INCLUDE_DIRS})
    target_link_libraries(ChatClient PRIVATE ${GLFW3_LIBRARIES} OpenGL::GL pthread dl)
endif()

# ==========================================
# TỰ ĐỘNG COPY RESOURCES (FONT, IMAGES...)
# ==========================================

# Lệnh này sẽ copy file SVN-Arial.ttf từ thư mục chứa code (CMAKE_SOURCE_DIR)
# vào thư mục chứa file exe (CMAKE_BINARY_DIR)
file(COPY "${CMAKE_SOURCE_DIR}/SVN-Arial.ttf" DESTINATION "${CMAKE_BINARY_DIR}")