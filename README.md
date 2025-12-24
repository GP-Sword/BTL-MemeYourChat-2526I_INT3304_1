# BTL-MemeYourChat-2526I_INT3304_1
BTL môn Lập trình mạng: Xây dựng chương trình chat sử dụng giao thức kiểu publish/subscribe.

## 15/12/25: Basic chatroom function


### How to run

Chỉnh lại include path của server.c. vì lí do nào đấy t k chạy dc khi để relative path.
1. gcc server/server.c common/net_utils.c -o server_chat.exe -lws2_32
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
```
