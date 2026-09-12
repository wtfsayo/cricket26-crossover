#!/usr/bin/env python3
"""Resume the recorded PC package download and verify its local reference checksum."""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import subprocess

NAME = 'BigbenInteractiveSA.3997493E05F07_3.69.0.0_x64__tqjv3vrxr8ppw.msixvc'
SIZE = 105586794496
SHA256 = 'd6d9b7569035c8b7045cc3af6647ed749b549bb7884ae5d15c0682cbb2cc6d72'
URL = 'http://assets1.xboxlive.com/2/f46d337f-391e-4ffa-acdc-7747e14ffde9/d218871b-8ad1-4494-81c3-59a9875af6c7/3.69.0.0.0d707f72-dcb6-4b29-96cd-d89959ad3316/' + NAME


def verify(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size != SIZE:
        raise SystemExit('Package size/type mismatch. Existing data preserved.')
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8*1024*1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != SHA256:
        raise SystemExit('Reference SHA-256 mismatch. Existing data preserved.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_root', type=Path)
    parser.add_argument('--verify-only', action='store_true', help='Verify an existing completed file without network access')
    args = parser.parse_args()
    root = args.game_root.expanduser().resolve()
    repo = Path(__file__).resolve().parent.parent
    if root == repo or repo in root.parents:
        parser.error('Download outside the source-only repository.')
    os.umask(0o077)
    root.mkdir(parents=True, exist_ok=True)
    with (root/'.download.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Another download helper is active.')
        target = root/NAME
        partial = root/(NAME+'.part')
        if target.exists() or target.is_symlink() or args.verify_only:
            verify(target)
            print('Completed package verified; no download needed.')
            return
        if partial.is_symlink() or (partial.exists() and not partial.is_file()):
            raise SystemExit('Unexpected partial file type; preserved.')
        if not partial.exists() or partial.stat().st_size < SIZE:
            subprocess.run(['/usr/bin/curl', '-4', '--fail', '--location', '--continue-at', '-', '--retry', '8', '--retry-delay', '5', '--retry-all-errors', '--connect-timeout', '20', '--speed-limit', '1024', '--speed-time', '120', '--output', str(partial), URL], check=True)
        verify(partial)
        # Hard linking fails if another program creates the final name meanwhile.
        os.link(partial, target)
        partial.unlink()
        print('Package verified against the local reference SHA-256. This is not a publisher-signed checksum.')


if __name__ == '__main__':
    main()
