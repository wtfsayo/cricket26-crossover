#!/bin/sh
set -eu
cd @GAME_ROOT@/runtime-research/winegdk-build
export PATH="$PWD/toolbin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/llvm/bin:/usr/bin:/bin:/usr/sbin:/sbin"
exec make -j6 dlls/xgameruntime/all dlls/windows.web/all
