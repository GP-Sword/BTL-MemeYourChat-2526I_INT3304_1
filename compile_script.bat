gcc server/server.c common/net_utils.c common/sqlite.c common/sqlite3.c -o server_chat.exe -lws2_32
gcc client/client.c common/net_utils.c -o client_chat.exe -lws2_32