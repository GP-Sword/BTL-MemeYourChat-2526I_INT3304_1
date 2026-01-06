#!/bin/bash

mkdir -p bin

echo "Compiling Server..."
gcc server/server.c \
    server/topic_svc.c \
    server/history.c \
    server/file_svc.c \
    libs/common/net_utils.c \
    libs/common/sqlite.c \
    libs/common/sqlite3.c \
    -o server_chat \
    -pthread -ldl -D_GNU_SOURCE

if [ $? -eq 0 ]; then
    echo "Server compiled successfully."
else
    echo "Server compilation FAILED."
    exit 1
fi

echo "Compiling Client..."
gcc client/client.c \
    libs/common/net_utils.c \
    -o client_chat \
    -pthread

if [ $? -eq 0 ]; then
    echo "Client compiled successfully."
else
    echo "Client compilation FAILED."
    exit 1
fi