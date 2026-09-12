#!/bin/sh
set -eu
cd @GAME_ROOT@/runtime-research/winegdk-build
export PATH="$PWD/toolbin:/opt/homebrew/opt/llvm/bin:/usr/bin:/bin"
exec ./tools/winegcc/winegcc -o ../game-window-probe.exe --wine-objdir . --cc-cmd="/opt/homebrew/opt/llvm/bin/clang -D__STDC__" -b x86_64-windows ../game-window-probe.c -Iinclude -I../winegdk-source/include -I../winegdk-source/include/msvcrt -D__WINESRC__ -nostdlib -Wl,-entry:mainCRTStartup dlls/user32/x86_64-windows/libuser32.a dlls/kernel32/x86_64-windows/libkernel32.a libs/winecrt0/x86_64-windows/libwinecrt0.a dlls/ntdll/x86_64-windows/libntdll.a dlls/ucrtbase/x86_64-windows/libucrtbase.a --no-default-config
