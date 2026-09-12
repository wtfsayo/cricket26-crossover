from pathlib import Path
import datetime
import hashlib
import sqlite3
import struct

root = Path(__file__).resolve().parent
out = root / 'extracted'
c = sqlite3.connect(root / 'progress.sqlite')
c.execute('CREATE TABLE IF NOT EXISTS extracted_files(path TEXT PRIMARY KEY, actual_bytes INTEGER, status TEXT)')
c.execute('CREATE TABLE IF NOT EXISTS executable_checks(path TEXT PRIMARY KEY, machine INTEGER, pe_signature_ok INTEGER, checked_at TEXT)')
rows = c.execute('SELECT path,expected_bytes FROM package_files').fetchall()
if len(rows) != 151041 or sum(size for _, size in rows) != 103991445917:
    raise SystemExit('Missing or different package inventory. Run scripts/inventory.py first.')
c.execute('DELETE FROM extracted_files')
c.execute('DELETE FROM executable_checks')
for name, expected in rows:
    path = out / name
    if not path.resolve().is_relative_to(out.resolve()):
        raise SystemExit('Inventory path escapes extraction directory.')
    actual = path.stat().st_size if path.is_file() else None
    status = 'ok' if actual == expected else ('missing' if actual is None else 'size mismatch')
    c.execute('INSERT OR REPLACE INTO extracted_files VALUES (?,?,?)', (name, actual, status))
for name, in c.execute("SELECT path FROM package_files WHERE lower(path) LIKE '%.exe'").fetchall():
    with (out / name).open('rb') as f:
        header = f.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise SystemExit(f'{name}: DOS signature missing')
        f.seek(struct.unpack_from('<I', header, 60)[0])
        pe = f.read(6)
        if len(pe) != 6 or pe[:4] != b'PE\0\0':
            raise SystemExit(f'{name}: PE signature missing')
        machine = struct.unpack_from('<H', pe, 4)[0]
        if machine != 0x8664:
            raise SystemExit(f'{name}: executable is not AMD64')
    c.execute('INSERT OR REPLACE INTO executable_checks VALUES (?,?,?,?)', (name, machine, 1, datetime.datetime.now(datetime.timezone.utc).isoformat()))
c.commit()
print('File verification:', c.execute('SELECT status,count(*) FROM extracted_files GROUP BY status').fetchall())
print('Total extracted GiB:', c.execute('SELECT round(sum(actual_bytes)/1073741824.0,3) FROM extracted_files').fetchone()[0])
print('Executable verification:', c.execute('SELECT path,machine,pe_signature_ok FROM executable_checks').fetchall())
failures = c.execute("SELECT path,status FROM extracted_files WHERE status!='ok'").fetchall()
if failures:
    print('First failures:', failures[:10])
    raise SystemExit(1)
exe = out / 'cricket26.exe'
digest = hashlib.sha256(exe.read_bytes()).hexdigest()
expected_digest = 'eb2a4db65977d15305136d8001466bcd2a9271b06216833b22c77833a07cb2b9'
if digest != expected_digest:
    raise SystemExit('cricket26.exe SHA-256 does not match the recorded decrypted executable.')
print('cricket26.exe SHA-256:', digest)
