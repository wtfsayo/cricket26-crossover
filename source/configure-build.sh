#!/bin/sh
set -eu
cd @GAME_ROOT@/runtime-research/winegdk-build
export PATH="$PWD/toolbin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/llvm/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export CC='/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang -arch x86_64 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk'
export CXX='/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ -arch x86_64 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk'
exec ../winegdk-source/configure --host=x86_64-apple-darwin27 --enable-win64 --disable-tests --without-x --without-freetype --without-gnutls --without-opengl --without-vulkan --without-gstreamer --without-ffmpeg
