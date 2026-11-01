#!/usr/bin/env python3
"""
Fork-only helper: export what native-tv/lifecycle/deploy.sh reads from a build,
so deploy.sh can run unchanged on a computer without the webOSbrew SDK
(Windows with Git Bash).

deploy.sh calls native-tv/tools/runtime_payload.py, which needs the three ARM
executables, CMakeCache.txt (it names the SDK sysroot), the licence cache, and
the SDK sysroot itself for the libraries, pkg-config versions and licence
notices it bundles. This copies exactly those, with the sysroot cut down to the
files runtime_payload resolves, then stages the runtime from the real SDK and
from the cut-down copy and fails unless the manifests (every file's SHA-256)
are identical.

    export-deploy-inputs.py native-tv/build-webos export/
        -> export/build-webos/        executables, CMakeCache.txt, licence cache
           export/build-webos-sdk/    the minimal SDK tree

Before running deploy.sh, point NATIVETV_SDK_SYSROOT in the exported
CMakeCache.txt at build-webos-sdk/<SYSROOT_RELATIVE> on that computer.
"""

import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'runtime_payload', ROOT / 'native-tv/tools/runtime_payload.py')
rp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rp)

NOTICE_NAMES = {"license", "copying", "notice", "copyright"}


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(Path(source).read_bytes())


def main():
    build = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    sysroot = rp._configured_sysroot(build)
    if not sysroot or not sysroot.is_dir():
        sys.exit('CMakeCache.txt names no SDK sysroot')
    sysroot = sysroot.resolve()
    sdk = sysroot.parents[1]
    shutil.rmtree(out, ignore_errors=True)
    out_build = out / 'build-webos'
    out_sdk = out / 'build-webos-sdk'
    mini_sysroot = out_sdk / sysroot.relative_to(sdk)

    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        # Staged from the real SDK first, as deploy.sh would on the build host;
        # this also fills the licence cache.
        real = {'native': rp.stage(build, temp / 'real-native'),
                'client': rp.stage_client(build, temp / 'real-client')}
        libraries = set(real['native']['libraries']) | set(real['client']['libraries'])

        for name in sorted(libraries):
            copy_file(rp._find_sdk_file(sysroot, name), mini_sysroot / 'lib' / name)
        for metadata in ('usr/lib/pkgconfig/libcurl.pc', 'usr/lib/pkgconfig/openssl.pc'):
            if (sysroot / metadata).is_file():
                copy_file(sysroot / metadata, mini_sysroot / metadata)
        for package in sorted({rp._license_package(name) for name in libraries}):
            for base in ('usr/share/licenses', 'usr/share/doc'):
                location = sysroot / base / package
                if location.is_file():
                    copy_file(location, mini_sysroot / base / package)
                elif location.is_dir():
                    for path in location.rglob('*'):
                        lower = path.name.lower()
                        if path.is_file() and (lower.split('.', 1)[0] in NOTICE_NAMES or
                                               lower.startswith(('license', 'copying', 'notice'))):
                            copy_file(path, mini_sysroot / path.relative_to(sysroot))
        includes = sdk / 'arm-webos-linux-gnueabi/include/c++'
        if includes.is_dir():
            for version in includes.iterdir():
                if version.is_dir():
                    marker = out_sdk / 'arm-webos-linux-gnueabi/include/c++' / version.name / '.keep'
                    marker.parent.mkdir(parents=True, exist_ok=True)
                    marker.write_text('')

        out_build.mkdir(parents=True)
        for name in ('nativetv-daemon', 'nativetv-service', 'nativetv-tv', 'CMakeCache.txt'):
            shutil.copy2(build / name, out_build / name)
        if (build / '.runtime-license-cache').is_dir():
            shutil.copytree(build / '.runtime-license-cache', out_build / '.runtime-license-cache')

        mini = {'native': rp.stage(out_build, temp / 'mini-native', mini_sysroot),
                'client': rp.stage_client(out_build, temp / 'mini-client', mini_sysroot)}
        for kind in real:
            if real[kind] != mini[kind]:
                sys.exit('the minimal SDK stages a different %s runtime' % kind)

    (out / 'SYSROOT_RELATIVE').write_text(sysroot.relative_to(sdk).as_posix() + '\n')
    print('exported to %s; libraries: %s' % (out, ', '.join(sorted(libraries))))


if __name__ == '__main__':
    main()
