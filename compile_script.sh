#!/bin/bash
gcc server/server.c server/topic_svc.c server/history.c server/file_svc.c libs/common/net_utils.c libs/common/sqlite.c libs/common/sqlite3.c -o server_chat -pthread -ldl
gcc client/client.c libs/common/net_utils.c -o client_chat -pthread