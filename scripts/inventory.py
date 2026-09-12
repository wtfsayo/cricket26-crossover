#!/usr/bin/env python3
"""Build the extraction inventory from package metadata, without acquiring a license."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import sqlite3
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game_root', type=Path)
parser.add_argument('inspect_exe', type=Path)
parser.add_argument('package', type=Path)
args = parser.parse_args()
root = args.game_root.expanduser().resolve()
repo = Path(__file__).resolve().parent.parent
if root == repo or repo in root.parents:
    parser.error('Keep inventory and game data outside this repository.')
os.umask(0o077)
result = subprocess.run([str(args.inspect_exe.expanduser().resolve()), str(args.package.expanduser().resolve())], env={**os.environ, 'XODUS_LOG':'off'}, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True)
metadata = json.loads(result.stdout)
rows = []
for entry in metadata['files']:
    name = entry['path']
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or type(entry['bytes']) is not int or entry['bytes'] < 0:
        raise SystemExit('Invalid inventory entry; database unchanged.')
    rows.append((name, entry['bytes'], int(entry['keep_encrypted'])))
if metadata['content_id'].lower() != 'd218871b-8ad1-4494-81c3-59a9875af6c7' or len(rows) != 151041 or sum(row[1] for row in rows) != 103991445917:
    raise SystemExit('Package inventory does not match the recorded build; database unchanged.')
root.mkdir(parents=True, exist_ok=True)
with sqlite3.connect(root/'progress.sqlite') as db:
    db.execute('CREATE TABLE IF NOT EXISTS package_files(path TEXT PRIMARY KEY, expected_bytes INTEGER, keep_encrypted INTEGER)')
    current = db.execute('SELECT path,expected_bytes,keep_encrypted FROM package_files').fetchall()
    if current:
        if sorted(current) != sorted(rows):
            raise SystemExit('Existing different inventory preserved.')
    else:
        db.executemany('INSERT INTO package_files VALUES(?,?,?)', rows)
    print('Inventory:', db.execute('SELECT count(*),sum(expected_bytes) FROM package_files').fetchone())
