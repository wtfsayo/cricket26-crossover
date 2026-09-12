#!/bin/sh
set -eu
cd @GAME_ROOT@/runtime-research/winegdk-build
export PATH="$PWD/toolbin:/opt/homebrew/opt/llvm/bin:/usr/bin:/bin"
exec ./tools/winegcc/winegcc -o ../runtime-auth-probe.exe --wine-objdir . --cc-cmd="/opt/homebrew/opt/llvm/bin/clang -D__STDC__" -b x86_64-windows ../runtime-auth-probe.c -Iinclude -I../winegdk-source/include -I../winegdk-source/include/msvcrt -D__WINESRC__ -nostdlib -Wl,-entry:mainCRTStartup dlls/kernel32/x86_64-windows/libkernel32.a --no-default-config
