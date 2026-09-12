#!/usr/bin/env python3
"""Offline packaging checks. No game launch, real download, or account access."""
import ast
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import runpy
import shutil
import tempfile
from unittest.mock import patch

repo = Path(__file__).resolve().parent.parent
for source in repo.rglob('*.py'):
    ast.parse(source.read_text(), filename=str(source))
spec = importlib.util.spec_from_file_location('download', repo/'scripts/download.py')
download = importlib.util.module_from_spec(spec)
spec.loader.exec_module(download)
download.NAME = 'fixture.package'
download.SIZE = 7
download.SHA256 = hashlib.sha256(b'fixture').hexdigest()

with tempfile.TemporaryDirectory(prefix='cricket-source-check-') as temp:
    base = Path(temp).resolve()
    game, xodus = base/'game space', base/'xodus space'
    prepare = ['python3', str(repo/'scripts/prepare.py'), str(game), '--xodus-source', str(xodus)]
    subprocess.run(prepare, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(prepare, check=True, stdout=subprocess.DEVNULL)
    for source in game.rglob('*'):
        if source.is_file() and (source.suffix == '.sh' or source.name == 'lld-link'):
            subprocess.run(['sh', '-n', str(source)], check=True)
    conflict = game/'runtime-research/winegdk-build/rebuild.sh'
    conflict.write_text('preserve this change\n')
    assert subprocess.run(prepare, capture_output=True).returncode
    assert conflict.read_text() == 'preserve this change\n'

    argv = ['download.py', str(game)]
    target = game/download.NAME
    target.write_bytes(b'fixture')
    with patch('sys.argv', argv), patch.object(download.subprocess, 'run') as network:
        download.main()
        network.assert_not_called()
    target.write_bytes(b'invalid')
    with patch('sys.argv', argv), patch.object(download.subprocess, 'run') as network:
        try:
            download.main()
            raise AssertionError('invalid completed file accepted')
        except SystemExit:
            pass
        network.assert_not_called()
        assert target.read_bytes() == b'invalid'
    target.unlink()
    (game/(download.NAME+'.part')).write_bytes(b'fixture')
    with patch('sys.argv', argv), patch.object(download.subprocess, 'run') as network:
        download.main()
        network.assert_not_called()
        assert target.read_bytes() == b'fixture'

    missing = subprocess.run(['python3', str(game/'verify-extraction.py')], capture_output=True)
    assert missing.returncode

    # Run staging against a disposable bottle with real files and configuration.
    fake_home = base/'home'
    bottle = fake_home/'Library/Application Support/CrossOver/Bottles/Cricket26-Runtime-Test'
    system32 = bottle/'drive_c/windows/system32'
    system32.mkdir(parents=True)
    (system32/'xgameruntime.dll.threading').write_bytes(b'official-file-placeholder')
    runtime = game/'runtime-research/winegdk-runtime'
    build = game/'runtime-research/winegdk-build'
    inputs = [
        (build/'dlls/xgameruntime/x86_64-windows/xgameruntime.dll', b'PE-fixture'),
        (build/'dlls/xgameruntime/xgameruntime.so', b'MachO-fixture'),
        (build/'dlls/windows.web/x86_64-windows/windows.web.dll', bytes(64)+b'Wine builtin DLL'+b'end'),
    ]
    for path, data in inputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    configuration = bottle/'cxbottle.conf'
    configuration.write_text(f'[Wine]\n# {runtime}\n"DllPath" = "wrong"\n')
    stage_script = game/'runtime-research/stage-winegdk.py'
    with patch.object(Path, 'home', return_value=fake_home), patch.object(subprocess, 'check_output', return_value=''):
        try:
            runpy.run_path(str(stage_script), run_name='__main__')
            raise AssertionError('ineffective DllPath accepted')
        except SystemExit:
            pass
    assert not runtime.exists()
    configuration.write_text(f'[Wine]\n"DllPath" = "{runtime}:{runtime}/x86_64-windows:other"\n')
    (runtime/'x86_64-windows').mkdir(parents=True)
    victim = base/'preserve-target'
    victim.write_bytes(b'untouched')
    legacy_temp = runtime/'x86_64-windows/xgameruntime.dll.stage-new'
    legacy_temp.symlink_to(victim)
    with patch.object(Path, 'home', return_value=fake_home), patch.object(subprocess, 'check_output', return_value=''):
        runpy.run_path(str(stage_script), run_name='__main__')
    assert victim.read_bytes() == b'untouched' and legacy_temp.is_symlink()
    assert (system32/'windows.web.dll').read_bytes()[64:80] == bytes(16)
    assert (runtime/'x86_64-windows/xgameruntime.dll').read_bytes() == b'PE-fixture'

    # Incidental files in a copied source folder must not enter the render plan.
    copy = base/'source-repo'
    shutil.copytree(repo/'source', copy/'source')
    (copy/'scripts').mkdir()
    shutil.copyfile(repo/'scripts/prepare.py', copy/'scripts/prepare.py')
    (copy/'source/__pycache__').mkdir(exist_ok=True)
    (copy/'source/.DS_Store').write_bytes(b'\x00')
    subprocess.run(['python3', str(copy/'scripts/prepare.py'), str(base/'another-game'), '--xodus-source', str(base/'another-xodus')], check=True, stdout=subprocess.DEVNULL)
print('Offline source packaging checks passed.')
