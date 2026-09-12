#!/usr/bin/env python3
"""Stage the locally built runtime into the existing isolated CrossOver bottle."""
from pathlib import Path
import datetime
import configparser
import hashlib
import os
import shutil
import sqlite3
import subprocess
import tempfile

root = Path(__file__).resolve().parent
bottle = Path.home() / 'Library/Application Support/CrossOver/Bottles/Cricket26-Runtime-Test'
system32 = bottle / 'drive_c/windows/system32'
stage = root / 'winegdk-runtime'
build = root / 'winegdk-build'
processes = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True)
if any('cricket26.exe' in line.lower() for line in processes.splitlines()):
    raise SystemExit('Exit Cricket 26 before staging the runtime.')
if not (system32 / 'xgameruntime.dll.threading').is_file():
    raise SystemExit('The isolated bottle must already contain the official threading DLL.')
config = configparser.ConfigParser(interpolation=None)
config.read(bottle / 'cxbottle.conf')
dll_path = config.get('Wine', '"DllPath"', fallback='').strip('"').split(':')
if dll_path[:2] != [str(stage), str(stage / 'x86_64-windows')]:
    raise SystemExit('The isolated bottle DllPath has not been configured.')

artifacts = [
    (build / 'dlls/xgameruntime/x86_64-windows/xgameruntime.dll', stage / 'x86_64-windows/xgameruntime.dll', False),
    (build / 'dlls/xgameruntime/xgameruntime.so', stage / 'x86_64-unix/xgameruntime.so', False),
    (build / 'dlls/windows.web/x86_64-windows/windows.web.dll', system32 / 'windows.web.dll', True),
]
# Validate all inputs before replacing any output.
for source, target, strip_marker in artifacts:
    if not source.is_file():
        raise SystemExit(f'Missing built artifact: {source}')
    if strip_marker and source.read_bytes()[64:80] != b'Wine builtin DLL':
        raise SystemExit('Unexpected windows.web PE header; refusing to alter it.')

with sqlite3.connect(root.parent / 'progress.sqlite') as db:
    db.execute('CREATE TABLE IF NOT EXISTS runtime_staged_artifacts (observed_at TEXT NOT NULL, path TEXT NOT NULL, sha256 TEXT NOT NULL, header_marker_removed INTEGER NOT NULL)')
    now = datetime.datetime.now(datetime.timezone.utc).isoformat()
    for source, target, strip_marker in artifacts:
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=target.name + '.stage-', dir=target.parent)
        temp = Path(temp_name)
        try:
            with os.fdopen(fd, 'w+b') as f, source.open('rb') as src:
                shutil.copyfileobj(src, f)
                if strip_marker:
                    # CrossOver otherwise redirects this DLL to its older bundled parser.
                    f.seek(64)
                    f.write(bytes(16))
                f.flush()
                os.fsync(f.fileno())
            os.chmod(temp, source.stat().st_mode & 0o777)
            os.replace(temp, target)
        finally:
            if temp.exists():
                temp.unlink()
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        db.execute('INSERT INTO runtime_staged_artifacts VALUES (?,?,?,?)', (now, str(target), digest, int(strip_marker)))
        print(f'{target.name}: {digest}')
