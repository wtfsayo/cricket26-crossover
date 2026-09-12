#!/bin/sh
set -eu
umask 077
ROOT=@GAME_ROOT@
if ps -axo comm= | /usr/bin/grep -iq '[c]ricket26.exe'; then
  echo "Cricket 26 is already running."
  exit 1
fi
stamp=$(date +%Y%m%d-%H%M%S)
export WINEGDK_LOCAL_GAMESAVE=1
exec /Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine \
  --bottle Cricket26-Runtime-Test --desktop Cricket26Window \
  --workdir "$ROOT/extracted" --dll 'xgameruntime=b;windows.web=n' \
  --debugmsg '-all' \
  --cx-log "$ROOT/cricket26-windowed-$stamp.log" \
  "$ROOT/extracted/cricket26.exe" > "$ROOT/cricket26-windowed-$stamp.out" 2>&1
