#!/bin/bash

# Exit immediately on error
set -e

# Compiler and flags
CC=gcc
CFLAGS="-g -DDEBUG=1 -Wall -Wextra -Werror -pedantic -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-declaration-after-statement -Wno-unused-variable -Wno-long-long -Wno-missing-field-initializers -Wno-implicit-function-declaration -Wno-unused-function -Wno-uninitialized"

# Create output directories if they don't exist
mkdir -p build/ArkSDK/include/ark build/ArkSDK/lib

echo "🔧 Compiling dynamic libraries..."

# JSON module
$CC $CFLAGS -fPIC -shared lib/json/json.c -o build/ArkSDK/lib/libark_json.so

# HTTP module
$CC $CFLAGS -fPIC -shared lib/http/http.c -o build/ArkSDK/lib/libark_http.so

# Memory module
$CC $CFLAGS -fPIC -shared lib/memory/memory.c -o build/ArkSDK/lib/libark_memory.so

# Template module
$CC $CFLAGS -fPIC -shared lib/template/template.c -o build/ArkSDK/lib/libark_template.so

# Server module
$CC $CFLAGS -fPIC -shared lib/server/server.c -o build/ArkSDK/lib/libark_server.so

# Profiler module
$CC $CFLAGS -fPIC -shared lib/profiler/profiler.c -o build/ArkSDK/lib/libark_profiler.so

# Utils module
$CC $CFLAGS -fPIC -shared lib/utils/utils.c -o build/ArkSDK/lib/libark_utils.so

# Task module
$CC $CFLAGS -fPIC -shared \
    lib/task/asm.S \
    lib/task/channel.c \
    lib/task/context.c \
    lib/task/fd.c \
    lib/task/net.c \
    lib/task/print.c \
    lib/task/qlock.c \
    lib/task/rendez.c \
    lib/task/task.c \
    -o build/ArkSDK/lib/libark_task.so

echo "📦 Packaging..."

# Copy public headers to SDK include folder
cp include/ark/*.h build/ArkSDK/include/ark/
cp -r src/assets/* build/assets

echo "✅ Build and packaging complete!"

SRC_FILES=$(find ./src -name "*.c")
$CC $CFLAGS $SRC_FILES -I./build/ArkSDK/include \
    -L./build/ArkSDK/lib -lark_server -lark_memory -lark_http -lark_utils -lark_template -lark_task -o ./build/tilastorage