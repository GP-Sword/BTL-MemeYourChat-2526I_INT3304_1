# BTL-MemeYourChat-2526I_INT3304_1
BTL môn Lập trình mạng: Xây dựng chương trình chat sử dụng giao thức kiểu publish/subscribe.

## 28/12/25: Sqlite login/register implementation
- Nhập username vào, nếu là user mới, sẽ ask for password để register. Nếu cũ, nhập pw cũ thì socket tiếp tục kết nối
- Vấn đề bây h: Wrong pw vẫn thấy các commands, chưa intuitive. K thấy list of registered users (very bad)

## 15/12/25: Basic chatroom function

## 1. Yêu cầu hệ thống (Prerequisites)

Trước khi bắt đầu, đảm bảo máy tính đã cài đặt các công cụ sau:

1.  **Visual Studio Code**: (Khuyên dùng) Cài thêm extension *C/C++* và *CMake Tools*.
2.  **MinGW-w64 (MSYS2)**: Bộ trình biên dịch GCC/G++ cho Windows.
    * Đảm bảo đã thêm đường dẫn `bin` (vd: `C:\msys64\ucrt64\bin`) vào biến môi trường **PATH**.
3.  **CMake**: [Tải tại đây](https://cmake.org/download/). Chọn bộ cài Windows x64 Installer.
4.  **Git**: Để clone dự án.

## 2. Project Structure
Sau khi setup xong, cấu trúc thư mục chuẩn phải trông như sau:

```text
Project/
├── server/
│   └── server.c
├── client/
│   ├── main.cpp          // Entry point + ImGui Loop
│   ├── net_logic.h       // Header hàm xử lý mạng
│   ├── net_logic.cpp     // Logic mạng (Cải biên từ client.c)
│   └── state.h           // Struct dữ liệu chung (biến toàn cục)
├── libs/                   # Thư viện bên ngoài (Quan trọng!)
│   ├── imgui/              # Source code ImGui
│   │   ├── backends/       # BẮT BUỘC CÓ (chứa imgui_impl_glfw...)
│   │   ├── imgui.cpp
│   │   └── imgui.h ...
│   └── glfw/               # Thư viện đồ họa
│       ├── include/
│       └── lib-mingw-w64/  # Chứa file .a và .dll
└── CMakeLists.txt        // File cấu hình build (Dùng CMake cho dễ quản lý)

```


## 3. How to run
# 3.1. Backend
Chỉnh lại include path của server.c. vì lí do nào đấy t k chạy dc khi để relative path.
1. gcc server/server.c common/net_utils.c common/sqlite.c common/sqlite3.c `
  -o server_chat.exe `
  -lws2_32
2. gcc client/client.c common/net_utils.c -o client_chat.exe -lws2_32
3. Terminal 1: .\server_chat.exe 910
4. Terminal 2: .\client_chat.exe 127.0.0.1 910 đặt tên gì đó
5. Terminal 3: .\client_chat.exe 127.0.0.1 910 đặt tên gì đó khác

Expected output:
```
Enter your user ID: Alice
[CLIENT] Connected to 127.0.0.1:910
[CLIENT] Logged in as Alice. Current group: group/global
Commands:
  /join <groupName>         Join a group (topic = group/<groupName>)
  /pm <userId> <message>    Private message (topic = user/<userId>)
  /group <groupName>        Set current group (for normal messages)
  /quit                     Exit
  <text>                    Send to current group (default group/global)
text
[Bob -> group/global] <text>
[Bob -> user/Alice] sup cutie hyd
yeah
[CLIENT] Exiting...
[CLIENT] Disconnected from server.


send file (no quotation marks)

/filegrp global C:\Users\twtvf\OneDrive\Documents\GitHub\BTL-MemeYourChat-2526I_INT3304_1\gay.txt
```

# 3.2. Frontend:
```
B1: Clone dự án
B2: Tải thư viện phụ thuộc (Dependencies)
  Vì lý do tối ưu repository, bạn cần tải thủ công hoặc kiểm tra thư mục libs nếu thiếu: 
    ImGui: Tải source code tại: https://github.com/ocornut/imgui
      Giải nén, copy toàn bộ file .h và .cpp vào thư mục libs/imgui/.
      Lưu ý: Copy thư mục backends từ file tải về vào trong libs/imgui/backends.

    GLFW:
      Tải Windows Pre-compiled Binaries (64-bit) tại: https://www.glfw.org/download.html
      Giải nén, đổi tên folder thành glfw và đặt vào libs/.
B3: Build client
  Tại thư mục gốc, tạo thư mục build:
    mkdir build
    cd build

  Chạy lệnh cấu hình CMake (Sử dụng MinGW):
    cmake -G "MinGW Makefiles" ..
  
  Biên dịch ra file exe
    cmake --build .

B4: Chạy
Lưu ý quan trọng (Lỗi thiếu DLL)
Trước khi chạy Client, bạn phải copy file glfw3.dll từ libs/glfw/lib-mingw-w64/ vào thư mục build/ (nơi chứa file ChatClient.exe). Nếu không sẽ báo lỗi System Error.
```