#!/bin/bash

# Exit immediately on error
set -e

# Compiler and flags
CC=gcc
CFLAGS="-std=c89 -g -DDEBUG=1 -Wall -Wextra -Werror -pedantic -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-declaration-after-statement -Wno-unused-variable -Wno-long-long -Wno-missing-field-initializers -Wno-implicit-function-declaration"

# Create output directories if they don't exist
mkdir -p lib
mkdir -p ArkSDK/include/ark
mkdir -p ArkSDK/lib

echo "🔧 Compiling dynamic libraries..."

# JSON module
$CC $CFLAGS -fPIC -c src/json/json.c -o lib/json.o
$CC -shared lib/json.o -o lib/libark_json.so

# HTTP module
$CC $CFLAGS -fPIC -c src/http/http.c -o lib/http.o
$CC -shared lib/http.o -o lib/libark_http.so

# Memory module
$CC $CFLAGS -fPIC -c src/memory/memory.c -o lib/memory.o
$CC -shared lib/memory.o -o lib/libark_memory.so

# Template module
$CC $CFLAGS -fPIC -c src/template/template.c -o lib/template.o
$CC -shared lib/template.o -o lib/libark_template.so

# Server module
$CC $CFLAGS -fPIC -c src/server/server.c -o lib/server.o
$CC -shared lib/server.o -o lib/libark_server.so

# Profiler module
$CC $CFLAGS -fPIC -c src/profiler/profiler.c -o lib/profiler.o
$CC -shared lib/profiler.o -o lib/libark_profiler.so

# Utils module
$CC $CFLAGS -fPIC -c src/utils/utils.c -o lib/utils.o
$CC -shared lib/utils.o -o lib/libark_utils.so

echo "📦 Packaging SDK..."

# Copy public headers to SDK include folder
cp include/ark/*.h ArkSDK/include/ark/

# Copy shared libraries to SDK lib folder
cp lib/*.so ArkSDK/lib/

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