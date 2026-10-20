#!/bin/sh
set -eu
SDK=/mnt/openipc-persist/sdk-rtsp
while IFS=' ' read -r kind name; do
    case "$name" in ''|.|..|/*|./*|../*|*/./*|*/../*|*/..|*//*|*[!a-zA-Z0-9_.+/+-]*) exit 1 ;; esac
    case "$kind" in
        D) [ -d "$SDK/$name" ] && [ ! -L "$SDK/$name" ] ;;
        F) [ -L "$SDK/$name" ] &&
           [ "$(/bin/busybox readlink "$SDK/$name")" = "/app/runtime/sdk-rtsp/$name" ] ;;
        *) exit 1 ;;
    esac
done < /app/runtime/SDK-INDEX
