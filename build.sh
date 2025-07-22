#!/bin/bash

# Exit immediately on error
set -e

# Compiler and flags
CC=gcc
CFLAGS="-std=c89 -g -DDEBUG=1 -Wall -Wextra -Werror -pedantic -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-declaration-after-statement -Wno-unused-variable -Wno-long-long -Wno-missing-field-initializers -Wno-implicit-function-declaration"

# Create output directories if they don't exist
mkdir -p ArkSDK/include/ark
mkdir -p ArkSDK/lib

echo "🔧 Compiling dynamic libraries..."

# JSON module
$CC $CFLAGS -fPIC -shared src/json/json.c -o ArkSDK/lib/libark_json.so

# HTTP module
$CC $CFLAGS -fPIC -shared src/http/http.c -o ArkSDK/lib/libark_http.so

# Memory module
$CC $CFLAGS -fPIC -shared src/memory/memory.c -o ArkSDK/lib/libark_memory.so

# Template module
$CC $CFLAGS -fPIC -shared src/template/template.c -o ArkSDK/lib/libark_template.so

# Server module
$CC $CFLAGS -fPIC -shared src/server/server.c -o ArkSDK/lib/libark_server.so

# Profiler module
$CC $CFLAGS -fPIC -shared src/profiler/profiler.c -o ArkSDK/lib/libark_profiler.so

# Utils module
$CC $CFLAGS -fPIC -shared src/utils/utils.c -o ArkSDK/lib/libark_utils.so

echo "📦 Packaging SDK..."

# Copy public headers to SDK include folder
cp include/ark/*.h ArkSDK/include/ark/

echo "✅ Build and packaging complete!"

echo "🧪 Compiling tests..."

$CC $CFLAGS -I./ArkSDK/include test/test_json.c \
    -L./ArkSDK/lib -lark_json -o test/test_json

$CC $CFLAGS -I./ArkSDK/include test/test_http.c \
    -L./ArkSDK/lib -lark_http -o test/test_http

$CC $CFLAGS -I./ArkSDK/include test/test_server.c \
    -L./ArkSDK/lib -lark_server -lark_memory -lark_utils -o test/test_server

SRC_FILES=$(find ./app/src -name "*.c")
$CC $CFLAGS $SRC_FILES -I./ArkSDK/include \
    -L./ArkSDK/lib -lark_server -lark_memory -lark_http -lark_utils -lark_template -o ./app/bin

echo "🚀 Running test..."
LD_LIBRARY_PATH=$(pwd)/ArkSDK/lib ./test/test_json
LD_LIBRARY_PATH=$(pwd)/ArkSDK/lib ./test/test_http