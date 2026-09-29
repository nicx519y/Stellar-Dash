"""Generate build identity before compiling STM32 A/B and TX. No device access."""
import argparse
import hashlib
import pathlib
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', args.version):
        parser.error('version must be major.minor.patch')
    parts = [int(p) for p in args.version.split('.')]
    if any(p > 255 for p in parts):
        parser.error('component versions must fit the existing CAPS bytes')
    root = pathlib.Path(__file__).resolve().parents[1]
    target = root / 'common/release_build_identity.h'
    names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root, timeout=30).decode().split('\0')
    digest = hashlib.sha256(args.version.encode())
    for name in sorted(set(names)):
        file = root / name
        source = file.suffix.lower() in {'.c', '.cpp', '.h', '.hpp', '.s', '.ld', '.mk', '.inc'} or file.name == 'Makefile'
        if source and name and file.is_file() and file != target and name.startswith(('application/', 'common/', 'RF_PHY_Hop/')) and '/www/' not in name:
            digest.update(name.encode())
            digest.update(file.read_bytes())
    build_id = digest.hexdigest()
    target.write_text('#ifndef XORA_RELEASE_BUILD_IDENTITY_H\n#define XORA_RELEASE_BUILD_IDENTITY_H\n'
                      f'#define XORA_RELEASE_VERSION "{args.version}"\n#define XORA_RELEASE_BUILD_ID "{build_id}"\n'
                      + ''.join(f'#define XORA_RELEASE_VERSION_{key} {value}\n' for key, value in zip(('MAJOR', 'MINOR', 'PATCH'), parts))
                      + '#define XORA_RELEASE_IDENTITY_READY 1\n#endif\n', encoding='utf-8')
    print(f'XORA {args.version} build {build_id}; compile both slots and TX using this identity.')


if __name__ == '__main__':
    main()
