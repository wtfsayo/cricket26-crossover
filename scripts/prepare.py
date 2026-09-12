#!/usr/bin/env python3
"""Render the preserved scripts into an external game workspace without overwrites."""
import argparse
from pathlib import Path
import shlex

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game_root', type=Path)
parser.add_argument('--xodus-source', type=Path, default=Path.home()/'.local/share/xodus/source')
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
root = args.game_root.expanduser().resolve()
xodus = args.xodus_source.expanduser().resolve()
if root == repo or repo in root.parents or xodus == repo or repo in xodus.parents:
    parser.error('Game and dependency workspaces must be outside this source-only repository.')
plan = []
for src in sorted((repo/'source').iterdir()):
    name = src.name
    if not src.is_file() or src.is_symlink() or not (src.suffix in {'.c', '.rs', '.py', '.sh', '.patch'} or name in {'Cargo.lock', 'lld-link'}):
        continue
    if name.endswith('.patch'):
        continue
    if name == 'Cargo.lock':
        dest = xodus/name
    elif name.endswith('.rs'):
        dest = xodus/'crates/xodus-cli/examples'/name
    elif name == 'lld-link':
        dest = root/'runtime-research/winegdk-build/toolbin'/name
    elif name.endswith('.sh') and name != 'launch-local-windowed.sh':
        dest = root/'runtime-research/winegdk-build'/name
    elif name == 'verify-extraction.py':
        dest = root/name
    else:
        dest = root/'runtime-research'/name
    data = src.read_text().replace('@GAME_ROOT@', shlex.quote(str(root))).replace('@HOME@', shlex.quote(str(Path.home())))
    if dest.exists() or dest.is_symlink():
        if dest.is_symlink() or not dest.is_file() or dest.read_text() != data:
            parser.error(f'Existing different file preserved: {dest}. Move it aside deliberately before replay.')
    plan.append((dest, data))
for dest, data in plan:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        with dest.open('x') as f:
            f.write(data)
        dest.chmod(0o700 if dest.suffix == '.sh' or dest.name == 'lld-link' else 0o600)
print(f'Prepared scripts in {root}; dependency inputs in {xodus}. No builds or game launches performed.')
