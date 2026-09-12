#!/usr/bin/env python3
"""Request a real license via the local service; never persist or print its token."""
import argparse
import os
from pathlib import Path
import socket
import sqlite3
import stat
import struct
import time
import uuid
import xml.etree.ElementTree as ET

MAGIC = 0x58445358
ROOT = Path(__file__).resolve().parent.parent


def exchange(path, message_type, root):
    payload = ET.tostring(root, encoding='utf-8')
    if len(payload) > 65535:
        raise ValueError('request too large')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(35)
        connection.connect(path)
        connection.sendall(struct.pack('<IHH', MAGIC, message_type, len(payload)) + payload)
        def read_exact(size):
            result = bytearray()
            while len(result) < size:
                chunk = connection.recv(size - len(result))
                if not chunk:
                    raise ValueError('incomplete service response')
                result.extend(chunk)
            return result
        magic, kind, size = struct.unpack('<IHH', read_exact(8))
        if magic != MAGIC or kind != message_type + 1 or not size:
            raise ValueError('invalid service response envelope')
        body = read_exact(size)
        if b'<!DOCTYPE' in body or b'<!ENTITY' in body:
            raise ValueError('invalid XML response')
        try:
            response = ET.fromstring(body)
        finally:
            body[:] = b'\x00' * len(body)
        return response


def record(phase, hr, size, elapsed):
    with sqlite3.connect(ROOT / 'progress.sqlite') as db:
        db.execute('CREATE TABLE IF NOT EXISTS headless_store_probes (observed_at TEXT NOT NULL, phase TEXT NOT NULL, hresult INTEGER NOT NULL, token_bytes INTEGER NOT NULL, elapsed_seconds REAL NOT NULL)')
        db.execute("INSERT INTO headless_store_probes VALUES(datetime('now'),?,?,?,?)", (phase, hr, size, elapsed))
    print(f'{phase}: HRESULT=0x{hr:08x} token_bytes={size} elapsed={elapsed:.2f}s', flush=True)


def hresult(response, expected_root):
    if response.tag != expected_root:
        raise ValueError('unexpected response type')
    value = int(response.findtext('HResult', '-1'))
    if not 0 <= value <= 0xffffffff:
        raise ValueError('invalid HRESULT')
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--socket', default='/tmp/xodus.sock')
    args = parser.parse_args()
    info = os.stat(args.socket, follow_symlinks=False)
    if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o600:
        raise ValueError('service socket ownership or permissions invalid')
    start = time.monotonic()
    context = exchange(args.socket, 5, ET.Element('StoreContextRequest'))
    hr = hresult(context, 'StoreContextResponse')
    record('context', hr, 0, time.monotonic() - start)
    if hr:
        return 2
    binding = context.findtext('AccountBinding', '')
    if str(uuid.UUID(binding)) != binding:
        raise ValueError('invalid account binding')
    root = ET.Element('LicenseTokenRequest')
    for key, value in [('AccountBinding', binding), ('ParentProductId', '9N6JF50HZFW9'), ('ProductId', '9N6JF50HZFW9'), ('CustomDeveloperString', 'Cricket26-local-diagnostic-' + str(uuid.uuid4()))]:
        ET.SubElement(root, key).text = value
    start = time.monotonic()
    response = exchange(args.socket, 7, root)
    hr = hresult(response, 'LicenseTokenResponse')
    token = response.findtext('Token', '')
    token_size = len(token.encode('utf-8'))
    record('real_license_request', hr, token_size, time.monotonic() - start)
    if hr and token:
        raise ValueError('failed response contained a token')
    if not hr and (not token or token_size > 49152):
        raise ValueError('successful response token invalid')
    response.clear()
    del token
    return 0 if not hr else 2


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, ET.ParseError):
        print('probe failed: invalid or unavailable service response; sensitive details omitted', flush=True)
        raise SystemExit(1)
