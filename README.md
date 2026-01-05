# BTL-MemeYourChat-2526I_INT3304_1
BTL môn Lập trình mạng: Xây dựng chương trình chat sử dụng giao thức kiểu publish/subscribe.

## 28/12/25: Sqlite login/register implementation
- Nhập username vào, nếu là user mới, sẽ ask for password để register. Nếu cũ, nhập pw cũ thì socket tiếp tục kết nối
- Vấn đề bây h: Wrong pw vẫn thấy các commands, chưa intuitive. K thấy list of registered users (very bad)

## 15/12/25: Basic chatroom function


### How to run

1. gcc server/server.c common/net_utils.c common/sqlite.c common/sqlite3.c -o server_chat.exe -lws2_32
2. gcc client/client.c common/net_utils.c -o client_chat.exe -lws2_32
3. Terminal 1: .\server_chat.exe 910
4. Terminal 2: .\client_chat.exe 127.0.0.1 910 \[username_1\]
5. Terminal 3: .\client_chat.exe 127.0.0.1 910 \[username_2\]

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
