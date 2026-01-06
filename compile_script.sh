#!/bin/bash

mkdir -p bin

BUILD_CHAT=false
BUILD_GAME=false
PROVIDED_ARGS=false

for arg in "$@"
do
    if [ "$arg" == "--chat" ]; then
        BUILD_CHAT=true
        PROVIDED_ARGS=true
    elif [ "$arg" == "--game" ]; then
        BUILD_GAME=true
        PROVIDED_ARGS=true
    else
        echo "Error: Unknown argument '$arg'"
        echo "Usage: ./compile_script.sh [--chat] [--game]"
        exit 1
    fi
done

if [ "$PROVIDED_ARGS" = false ]; then
    BUILD_CHAT=true
    BUILD_GAME=true
fi

if [ "$BUILD_CHAT" = true ]; then
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
        echo "   - Server compiled successfully."
    else
        echo "   - [ERROR] Server compilation FAILED."
        exit 1
    fi

    echo "Compiling Client..."
    gcc client/client.c \
        libs/common/net_utils.c \
        -o client_chat \
        -pthread

    if [ $? -eq 0 ]; then
        echo "   - Client compiled successfully."
    else
        echo "   - [ERROR] Client compilation FAILED."
        exit 1
    fi
    echo ""
fi

if [ "$BUILD_GAME" = true ]; then
    echo "Compiling Game Server..."
    gcc game/game_server.c -o game_server -pthread

    if [ $? -eq 0 ]; then
        echo "   - Game server compiled successfully."
    else
        echo "   - [ERROR] Game server compilation FAILED."
        exit 1
    fi

    echo "Compiling Game Client (Linux Native)..."
    gcc game/game_client_linux.c -o game_client -lglfw -lGL -lm -pthread

    if [ $? -eq 0 ]; then
        echo "   - Game Client compiled successfully."
    else
        echo "   - [ERROR] Game Client compilation FAILED."
        echo "     Make sure you installed: sudo apt-get install libglfw3-dev libgl1-mesa-dev"
        exit 1
    fi
fi

echo "Build Process Finished."