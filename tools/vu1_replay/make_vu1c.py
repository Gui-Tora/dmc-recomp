"""Generate the compiled VU1 programs of Devil May Cry locally, from the user's own ELF.

    python tools/vu1_replay/make_vu1c.py [--elf original/SLES_503.58] [--out recomp/vu1c]

For every image of dmc_vu1_programs.json: verifies the ELF identity (SHA-256), rebuilds the 16 KiB VU1
micro memory by applying the listed VIF1 MPG commands found in the ELF file, checks the image hash the
runtime dispatcher keys on, and writes recomp/vu1c/vu1c_<hash>.cpp with gen_vu1c.py. Nothing derived
from the game is stored in the repository: the output directory is ignored by Git, and the build
(CMakeLists.txt) compiles whatever .cpp it finds there. Without the generated files the game builds and
runs with the interpreter.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from gen_vu1c import fnv1a64, write_cpp  # noqa: E402

VIF_MPG = 0x4A
MICRO_SIZE = 0x4000


def apply_mpg(elf, offset, image):
    code = struct.unpack_from('<I', elf, offset)[0]
    if (code >> 24) & 0x7F != VIF_MPG:
        raise SystemExit(f'0x{offset:x}: VIF code 0x{code:08x} is not MPG')
    count = (code >> 16) & 0xFF or 256
    dest = (code & 0xFFFF) * 8
    data = offset + 4
    if data % 8:
        data += 4  # MPG data follows 64-bit aligned
    if dest + count * 8 > MICRO_SIZE:
        raise SystemExit(f'0x{offset:x}: MPG outside micro memory')
    image[dest:dest + count * 8] = elf[data:data + count * 8]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', default=str(ROOT / 'original/SLES_503.58'))
    ap.add_argument('--out', default=str(ROOT / 'recomp/vu1c'))
    ap.add_argument('--manifest', default=str(HERE / 'dmc_vu1_programs.json'))
    args = ap.parse_args()
    manifest = json.loads(Path(args.manifest).read_text(encoding='utf-8'))
    elf = Path(args.elf).read_bytes()
    sha = hashlib.sha256(elf).hexdigest()
    if sha != manifest['elf']['sha256']:
        raise SystemExit(f'{args.elf}: SHA-256 {sha} is not {manifest["elf"]["name"]} '
                         f'{manifest["elf"]["sha256"]}; nothing generated')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for img in manifest['images']:
        image = bytearray(MICRO_SIZE)
        for off in img['vif_mpg_file_offsets']:
            apply_mpg(elf, int(off, 16), image)
        got = f'{fnv1a64(image):016x}'
        if got != img['fnv1a64']:
            raise SystemExit(f'rebuilt image hash {got} != expected {img["fnv1a64"]}; nothing generated')
        entries = [(e['kind'], int(e['pc'], 16)) for e in img['entries']]
        target = out / f'vu1c_{got}.cpp'
        compiled, rejected = write_cpp(bytes(image), entries, target)
        if rejected or len(compiled) != len(entries):
            raise SystemExit(f'image {got}: entries rejected by the generator: {rejected}')
        print(f'{target}: image {got}, entries {[(k, hex(p)) for k, p in compiled]}')


if __name__ == '__main__':
    main()
