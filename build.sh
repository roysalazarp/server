#!/bin/bash

# Exit immediately on error
set -e

# Compiler
CC=gcc

# Flags
DEV_CFLAGS="-g -DDEBUG=1 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-but-set-variable \
    -Wno-declaration-after-statement -Wno-unused-variable \
    -Wno-long-long -Wno-missing-field-initializers \
    -Wno-implicit-function-declaration -Wno-unused-function \
    -Wno-uninitialized -Wno-sign-compare"

PROD_CFLAGS="-O2 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-but-set-variable \
    -Wno-declaration-after-statement -Wno-unused-variable \
    -Wno-long-long -Wno-missing-field-initializers \
    -Wno-implicit-function-declaration -Wno-unused-function \
    -Wno-uninitialized -Wno-sign-compare"

# Build function
build_env() {
    ENV_DIR=$1
    CFLAGS=$2

    echo "📂 Building $ENV_DIR environment..."

    INCLUDE_DIR=$ENV_DIR/ArkSDK/include/ark
    LIB_DIR=$ENV_DIR/ArkSDK/lib
    ASSETS_DIR=$ENV_DIR/assets

    mkdir -p "$INCLUDE_DIR" "$LIB_DIR" "$ASSETS_DIR"

    echo "🔧 Compiling dynamic libraries for $ENV_DIR..."

    $CC $CFLAGS -fPIC -shared lib/json/json.c -o $LIB_DIR/libark_json.so
    
    $CC $CFLAGS -fPIC -shared \
        lib/http/gethttpheader.c \
        lib/http/khttprepeatable.c \
        lib/http/khttptoken.c \
        lib/http/ktolower.c \
        lib/http/parsehttpmessage.c \
        -o $LIB_DIR/libark_http.so
    
    $CC $CFLAGS -fPIC -shared lib/memory/memory.c -o $LIB_DIR/libark_memory.so
    $CC $CFLAGS -fPIC -shared lib/template/template.c -o $LIB_DIR/libark_template.so
    $CC $CFLAGS -fPIC -shared lib/server/server.c -o $LIB_DIR/libark_server.so
    $CC $CFLAGS -fPIC -shared lib/profiler/profiler.c -o $LIB_DIR/libark_profiler.so
    $CC $CFLAGS -fPIC -shared lib/utils/utils.c -o $LIB_DIR/libark_utils.so

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
        -o $LIB_DIR/libark_task.so

    echo "📦 Packaging headers and assets for $ENV_DIR..."
    cp include/ark/*.h "$INCLUDE_DIR/"
    cp -r src/assets/* "$ASSETS_DIR/"

    echo "🚀 Building main program for $ENV_DIR..."
    SRC_FILES=$(find ./src -name "*.c")
    $CC $CFLAGS $SRC_FILES -I$ENV_DIR/ArkSDK/include \
        -L$LIB_DIR -lark_server -lark_memory -lark_http -lark_utils -lark_template -lark_task -lsqlite3 \
        -o $ENV_DIR/tilastorage

    echo "✅ $ENV_DIR build complete!"
}

# Run both builds
build_env build "$PROD_CFLAGS"
build_env dev "$DEV_CFLAGS"

echo "🎉 All builds done!"
