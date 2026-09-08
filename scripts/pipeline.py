"""Windows bring-up orchestration; Python 3.11+, standard library only."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import tomllib
import zlib

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'vendor/PS2Recomp'
LOCAL = ROOT / 'analysis/local'
LOCK = json.loads((ROOT / 'upstream.lock.json').read_text())
PARALLEL_JOBS = max(1, (os.cpu_count() or 4) // 2)


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def identity(path):
    path = path.resolve(strict=True)
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Se requiere ELF32 little-endian, no una ISO.')
    if struct.unpack_from('<H', data, 18)[0] != 8:
        raise ValueError('El ELF no declara arquitectura MIPS.')
    return dict(path=path.as_posix(), name=path.name, size=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                crc32=f'{zlib.crc32(data):08X}',
                entry=f'{struct.unpack_from("<I", data, 24)[0]:08X}')


def run(args, label, timeout=None, cwd=ROOT):
    log = ROOT / 'logs' / f'{time.time_ns()}-{label}.log'
    log.parent.mkdir(exist_ok=True)
    print(f'{label}: {subprocess.list2cmdline([str(a) for a in args])}\nLog: {log}', flush=True)
    with log.open('w', encoding='utf-8') as out:
        out.write(json.dumps([str(a) for a in args]) + '\n')
        out.flush()
        try:
            result = subprocess.run([str(a) for a in args], cwd=cwd, stdout=out,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            out.write('\nTIMEOUT: proceso detenido; no demuestra un hito.\n')
            raise RuntimeError(f'Tiempo agotado; revisar {log}')
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}; revisar {log}')
    return log


def effective_commit():
    # With no patches[], vendor must sit exactly at the pinned upstream commit
    # (original behavior). With patches[], vendor must sit exactly at
    # patched_commit: a synthetic, deterministic commit derived from `commit` +
    # patches[] applied in order (see apply_patchset). `commit` itself always
    # stays the real, remote-recoverable upstream baseline; it is never
    # replaced by a local/synthetic hash.
    patches = LOCK.get('patches', [])
    if not patches:
        return LOCK['commit']
    if 'patched_commit' not in LOCK:
        raise ValueError('upstream.lock.json declara patches[] pero no patched_commit.')
    return LOCK['patched_commit']


def upstream():
    actual = subprocess.check_output(['git', '-C', str(VENDOR), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(VENDOR), 'status', '--porcelain'], text=True).strip()
    if actual != effective_commit() or dirty:
        raise ValueError('Upstream difiere del lock o tiene cambios; revisar antes de continuar.')


def verify_patchset_hashes():
    # Preflight: check every patch file exists and matches its recorded SHA256
    # BEFORE touching the working tree. Returns the resolved absolute paths in
    # LOCK['patches'] order (patches[].path is relative to ROOT, not to
    # vendor's cwd, so callers must use these resolved paths with `git apply`,
    # never the raw relative string).
    resolved = []
    for entry in LOCK.get('patches', []):
        patch_path = (ROOT / entry['path']).resolve()
        if not patch_path.is_file():
            raise ValueError(f"Patch declarado en upstream.lock.json no existe: {entry['path']}")
        actual_sha = digest(patch_path)
        if actual_sha != entry['sha256']:
            raise ValueError(
                f"SHA256 de {entry['path']} no coincide con upstream.lock.json "
                f"(esperado {entry['sha256']}, actual {actual_sha}); revisar antes de aplicar nada.")
        resolved.append(patch_path)
    return resolved


def apply_patchset(dest):
    # Applies LOCK['patches'] (in order) on top of the just-checked-out
    # baseline commit, then folds the result into one deterministic commit
    # (patched_commit) via commit-tree — never `git commit`, so this never
    # depends on user.name/user.email, commit hooks, commit.gpgsign, or any
    # local branch. Any failure resets vendor back to the clean baseline and
    # aborts explicitly; never leaves baseline + a partial subset of patches
    # applied.
    resolved_patches = verify_patchset_hashes()
    if not resolved_patches:
        return
    baseline = LOCK['commit']
    print(f'apply-patchset: {len(resolved_patches)} patch(es) sobre {baseline}', flush=True)

    def abort_and_reset(reason):
        subprocess.run(['git', '-C', str(dest), 'reset', '--hard', baseline],
                        capture_output=True, text=True)
        subprocess.run(['git', '-C', str(dest), 'clean', '-fd'],
                        capture_output=True, text=True)
        raise RuntimeError(reason)

    for patch_path in resolved_patches:
        print(f'  apply: {patch_path.relative_to(ROOT)}', flush=True)
        check = subprocess.run(['git', '-C', str(dest), 'apply', '--check', str(patch_path)],
                                capture_output=True, text=True)
        if check.returncode != 0:
            abort_and_reset(f'git apply --check fallo para {patch_path}:\n{check.stderr}')
        applied = subprocess.run(['git', '-C', str(dest), 'apply', str(patch_path)],
                                 capture_output=True, text=True)
        if applied.returncode != 0:
            abort_and_reset(f'git apply fallo para {patch_path}:\n{applied.stderr}')

    add = subprocess.run(['git', '-C', str(dest), 'add', '-A'], capture_output=True, text=True)
    if add.returncode != 0:
        abort_and_reset(f'git add -A fallo tras aplicar patches:\n{add.stderr}')

    tree = subprocess.check_output(['git', '-C', str(dest), 'write-tree'], text=True).strip()

    identity = LOCK['patch_identity']
    env = os.environ.copy()
    env.update(GIT_AUTHOR_NAME=identity['author_name'], GIT_AUTHOR_EMAIL=identity['author_email'],
               GIT_AUTHOR_DATE=identity['timestamp'], GIT_COMMITTER_NAME=identity['author_name'],
               GIT_COMMITTER_EMAIL=identity['author_email'], GIT_COMMITTER_DATE=identity['timestamp'])
    commit_tree = subprocess.run(
        ['git', '-C', str(dest), 'commit-tree', tree, '-p', baseline, '-m', identity['message']],
        capture_output=True, text=True, env=env)
    if commit_tree.returncode != 0:
        abort_and_reset(f'git commit-tree fallo:\n{commit_tree.stderr}')
    patched_commit = commit_tree.stdout.strip()

    if patched_commit != LOCK['patched_commit']:
        abort_and_reset(
            f"El patchset no reproduce patched_commit: esperado {LOCK['patched_commit']}, "
            f"obtenido {patched_commit}. Revisar patches[]/patch_identity/patched_commit en "
            f"upstream.lock.json (probable desincronizacion tras editar un patch).")

    checkout = subprocess.run(['git', '-C', str(dest), 'checkout', '--detach', patched_commit],
                              capture_output=True, text=True)
    if checkout.returncode != 0:
        abort_and_reset(f'git checkout --detach {patched_commit} fallo:\n{checkout.stderr}')
    print(f'apply-patchset: HEAD ahora en {patched_commit} (detached)', flush=True)


def cmake():
    found = shutil.which('cmake')
    if found:
        return found
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if vswhere.exists():
        base = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
        candidate = Path(base) / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if candidate.exists():
            return str(candidate)
    raise ValueError('Falta CMake >=3.21; instalar componente C++/CMake de Visual Studio.')


def executable(folder, name):
    matches = list(folder.rglob(name + '.exe'))
    if len(matches) != 1:
        raise ValueError(f'Se esperaba un ejecutable {name} en {folder}; encontrados {len(matches)}')
    return matches[0]


def bootstrap(_):
    for dest, url, commit in [(VENDOR, LOCK['repository'], LOCK['commit']),
                              (ROOT / 'vendor/PS2Recomp.wiki', LOCK['wiki_repository'], LOCK['wiki_commit'])]:
        if not dest.exists():
            # --no-checkout: no working tree is materialized yet, so
            # core.autocrlf can be fixed to a known value BEFORE any file
            # touches disk. Doing checkout first and configuring autocrlf
            # after is not equivalent: this repo inherits core.autocrlf=true
            # from a machine-wide gitconfig on at least one bring-up machine,
            # which silently rewrites line endings on checkout and makes
            # `git apply` fail against the LF-only patches in patches/.
            run(['git', 'clone', '--no-checkout', url, dest], 'clone')
            if dest == VENDOR:
                run(['git', '-C', dest, 'config', 'core.autocrlf', 'false'], 'configure-autocrlf')
            run(['git', '-C', dest, 'checkout', '--detach', commit], 'pin')
            run(['git', '-C', dest, 'submodule', 'update', '--init', '--recursive'], 'submodules')
            if dest == VENDOR:
                apply_patchset(dest)
    upstream()


def tools_build(_):
    upstream()
    build = ROOT / 'build/tools'
    run([cmake(), '-S', VENDOR, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
         '-DPS2X_BUILD_RUNTIME=OFF', '-DPS2X_BUILD_STUDIO=OFF', '-DPS2X_BUILD_TEST=OFF'], 'configure-tools')
    run([cmake(), '--build', build, '--config', 'Debug', '--target', 'ps2_recomp', 'ps2_analyzer',
         '--parallel', str(PARALLEL_JOBS)], 'build-tools')


def identify(args):
    value = identity(args.elf)
    save(LOCAL / 'elf.json', value)
    print(json.dumps(value, indent=2))


def prepare(args):
    upstream()
    value = identity(args.elf)
    source = args.toml.resolve(strict=True)
    raw = source.read_text(encoding='utf-8-sig')
    config = tomllib.loads(raw)
    source_elf = Path(config['general']['input'])
    if not source_elf.is_absolute():
        source_elf = source.parent / source_elf
    if digest(source_elf) != value['sha256']:
        raise ValueError('El ELF del export Ghidra no coincide con el ELF solicitado.')

    csv_path = None
    if args.csv is not None:
        csv_path = args.csv.resolve(strict=True)
        with csv_path.open(encoding='utf-8-sig', newline='') as stream:
            rows = list(csv.DictReader(stream))
        if not rows or not {'Name', 'Start', 'End', 'Size'} <= rows[0].keys():
            raise ValueError('CSV vacio o distinto del formato ExportPS2Functions.')
        for row in rows:
            if int(row['Start'], 0) >= int(row['End'], 0):
                raise ValueError('Rango CSV invalido.')

    output = ROOT / 'recomp/generated' / value['sha256'] / str(time.time_ns())
    substitutions = [('input', Path(value['path'])), ('output', output)]
    if csv_path is not None:
        # Only overwrite ghidra_output when a CSV was explicitly given; the default
        # symtab-first template already has ghidra_output = "" and stays that way.
        substitutions.append(('ghidra_output', csv_path))
    for key, path in substitutions:
        raw, count = re.subn(rf'(?m)^{key}\s*=.*$', lambda _: f'{key} = {json.dumps(path.as_posix())}', raw)
        if count != 1:
            raise ValueError(f'Campo {key} ausente o duplicado.')
    config_path = ROOT / 'recomp/config.toml'
    config_path.write_text(raw, encoding='utf-8')
    save(LOCAL / 'prepared.json', dict(elf=value, config_sha256=digest(config_path),
         csv_path=csv_path.as_posix() if csv_path else None,
         csv_sha256=digest(csv_path) if csv_path else None,
         upstream=LOCK['commit']))
    print('Configuracion preparada. Revisar stubs/untracked_stubs exportados antes de generar.')


def prepared():
    upstream()
    receipt = json.loads((LOCAL / 'prepared.json').read_text(encoding='utf-8'))
    if identity(Path(receipt['elf']['path'])) != receipt['elf']:
        raise ValueError('El ELF ha cambiado. Repetir Ghidra/prepare.')
    config_path = ROOT / 'recomp/config.toml'
    if digest(config_path) != receipt['config_sha256']:
        raise ValueError('Config ha cambiado. Repetir prepare.')
    if receipt['csv_path'] is not None and digest(Path(receipt['csv_path'])) != receipt['csv_sha256']:
        raise ValueError('CSV ha cambiado. Repetir prepare desde el export revisado.')
    return receipt, tomllib.loads(config_path.read_text(encoding='utf-8'))


def generated():
    receipt, config = prepared()
    gen = json.loads((LOCAL / 'generated.json').read_text(encoding='utf-8'))
    if gen['prepared'] != receipt:
        raise ValueError('Generacion obsoleta.')
    folder = Path(config['general']['output'])
    actual = {p.name: digest(p) for p in folder.iterdir() if p.suffix in ('.cpp', '.h')}
    if actual != gen['files']:
        raise ValueError('Codigo generado cambiado o incompleto; regenerar.')
    return receipt, folder, gen


def generate(_):
    receipt, config = prepared()
    folder = Path(config['general']['output'])
    if folder.exists() and any(folder.iterdir()):
        raise ValueError('Salida ya usada. Repetir prepare para crear una nueva sin archivos obsoletos.')
    run([executable(ROOT / 'build/tools', 'ps2_recomp'), ROOT / 'recomp/config.toml'], 'generate')
    for name in ('register_functions.cpp', 'ps2_recompiled_functions.h', 'ps2_recompiled_stubs.h'):
        if not (folder / name).is_file():
            raise ValueError(f'Falta salida {name}')
    table = (folder / 'register_functions.cpp').read_text()
    if not re.search(r'g_ps2RecompiledFunctionTable\[\d+\] = \w', table):
        raise ValueError('Tabla generada sin bindings.')
    save(LOCAL / 'generated.json', dict(prepared=receipt, files={p.name: digest(p) for p in folder.iterdir() if p.suffix in ('.cpp', '.h')}))


def sync_active(folder, files):
    # Mirrors the immutable, timestamped `generate` output into a stable directory so
    # unchanged files keep their path and mtime and MSBuild skips recompiling them.
    # The timestamped folder in recomp/generated/<sha256>/<timestamp> stays untouched
    # and remains the traceable source of truth (see generated.json / built.json).
    #
    # This points at analysis/local/symtabfirst/generated (not recomp/generated_active)
    # because that is where the symtab-first baseline's CMake cache and .vcxproj files
    # have absolute paths baked in (see analysis/notes/SYMTAB_BASELINE_PROMOTION.md).
    # Moving/renaming that directory would invalidate those paths and force a full
    # rebuild, so the "active" location is defined here instead of physically moved.
    active = ROOT / 'analysis/local/symtabfirst/generated'
    active.mkdir(parents=True, exist_ok=True)
    manifest_path = active / '.manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.is_file() else {}
    for name, sha in files.items():
        if manifest.get(name) != sha:
            shutil.copy2(folder / name, active / name)
            manifest[name] = sha
    for stale in set(manifest) - set(files):
        (active / stale).unlink(missing_ok=True)
        del manifest[stale]
    save(manifest_path, manifest)
    return active


def build(_):
    receipt, folder, gen = generated()
    active = sync_active(folder, gen['files'])
    # Same reasoning as sync_active(): the promoted symtab-first build's CMake cache
    # has C:/.../analysis/local/symtabfirst/build baked in absolute-path form.
    build_dir = ROOT / 'analysis/local/symtabfirst/build'
    run([cmake(), '-S', ROOT, '-B', build_dir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
         f'-DDMC_GENERATED_DIR={active.as_posix()}',
         f'-DDMC_MSVC_MP_JOBS={PARALLEL_JOBS}'], 'configure-runtime')
    run([cmake(), '--build', build_dir, '--config', 'Debug', '--target', 'ps2EntryRunner',
         '--parallel', str(PARALLEL_JOBS)], 'build-runtime')
    exe = executable(build_dir / 'bin', 'dmc-recomp')
    save(LOCAL / 'built.json', dict(generated=gen, exe=str(exe), exe_sha256=digest(exe)))


def launch(args):
    receipt, _, gen = generated()
    built = json.loads((LOCAL / 'built.json').read_text(encoding='utf-8'))
    exe = Path(built['exe'])
    if gen != built['generated'] or digest(exe) != built['exe_sha256']:
        raise ValueError('Ejecutable obsoleto; repetir build.')
    run([exe, receipt['elf']['path']], 'run', timeout=args.seconds,
        cwd=Path(receipt['elf']['path']).parent)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, function in [('bootstrap', bootstrap), ('tools', tools_build), ('generate', generate), ('build', build), ('run', launch)]:
        command = commands.add_parser(name)
        command.set_defaults(function=function)
        if name == 'run':
            command.add_argument('--seconds', type=int, default=60)
    command = commands.add_parser('identify')
    command.add_argument('elf', type=Path)
    command.set_defaults(function=identify)
    command = commands.add_parser('prepare')
    command.add_argument('--elf', type=Path, required=True)
    command.add_argument('--toml', type=Path, default=ROOT / 'recomp/symtabfirst.toml',
                          help='Default: symtab-first (no Ghidra CSV). Pass '
                               'analysis/ghidra/export/dmc.toml explicitly for the '
                               'Ghidra-CSV-boundaries experiment/reversing path.')
    command.add_argument('--csv', type=Path, default=None,
                          help='Only needed together with a --toml that has a non-empty '
                               'ghidra_output (e.g. analysis/ghidra/export/dmc.toml).')
    command.set_defaults(function=prepare)
    args = parser.parse_args()
    try:
        args.function(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
