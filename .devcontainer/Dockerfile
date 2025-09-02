# Start with the Debian 12 (bookworm) image
FROM mcr.microsoft.com/devcontainers/base:bookworm

RUN apt-get update && apt-get upgrade -y

RUN apt-get install -y \
    gdb \
    && apt-get clean
