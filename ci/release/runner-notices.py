#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Collect notices from the dependency selection of this release runner."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import urllib.request

import notices


def run(*command):
    return subprocess.check_output(command, text=True, encoding='utf8').strip()


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_record(destination, component, record, files, provenance='build-receipt'):
    directory = destination / component
    directory.mkdir(parents=True, exist_ok=True)
    evidence = directory / 'build-selection.json'
    evidence.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    return {'version': record['version'], 'source': record['source'],
            'source_sha256': record.get('archive_sha256', sha256(evidence)),
            'provenance_type': provenance, 'evidence': str(evidence.resolve()),
            'role': 'build-selected-dependency', 'files': [str(p.resolve()) for p in files]}


def source_notices(archive, expected_hash, destination):
    if not re.fullmatch(r'[0-9a-f]{64}', expected_hash or '') or sha256(archive) != expected_hash:
        raise ValueError(f'Source archive hash mismatch: {archive.name}')
    files = []
    with tarfile.open(archive) as package:
        for member in package:
            if not member.isfile():
                continue
            path = PurePosixPath(member.name)
            scan_path = PurePosixPath(member.name.replace('\\', '/'))
            name = scan_path.name.lower()
            selected = bool(re.fullmatch(r'(?:copying|licen[cs]e|copyright|notice)(?:[._-].*)?', name))
            selected |= 'LICENSES' in scan_path.parts or name in {'ftl.txt', 'gplv2.txt'}
            header = name == 'sqlite3.c' or scan_path.as_posix().endswith('/keysyms/keysyms.c')
            if not selected and not header:
                continue
            # Source tarballs can contain irrelevant manpage names such as
            # SystemTap's function::HZ.3stap. Never write those members.
            if path.is_absolute() or '..' in path.parts or '\\' in member.name or ':' in member.name:
                raise ValueError(f'Unsafe source archive notice: {archive.name}: {member.name}')
            data = package.extractfile(member).read()
            if header:
                data = data.split(b'*/', 1)[0] + b'*/\n'
            target = destination / Path(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            files.append(target)
    if not files:
        raise ValueError(f'No source license notices: {archive.name}')
    return files


def acquire(url, checksum, destination):
    if not url.startswith('https://'):
        raise ValueError('Source acquisition requires HTTPS')
    if not destination.exists():
        with urllib.request.urlopen(url, timeout=120) as response, destination.open('xb') as out:
            while data := response.read(1024 * 1024):
                out.write(data)
    if sha256(destination) != checksum:
        raise ValueError('Acquired source hash differs from installed dependency selection')
    return destination


def depends_notices(depends, host, destination):
    def variables(names, packages=()):
        bindings = []
        for name in names:
            package = next((p for p in sorted(packages, key=len, reverse=True)
                            if name.startswith(p + '_')), None)
            if package:
                # Recipes defer $(package)-based expansion until a target runs.
                bindings.append(f'--eval=print-{name}: package={package}')
        output = run('make', '--no-print-directory', '-C', str(depends), f'HOST={host}',
                     *bindings, *[f'print-{name}' for name in names])
        return dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
    selected = variables(['packages', 'host_prefix', 'SOURCES_PATH'])
    if not (Path(selected['host_prefix']) / 'toolchain.cmake').is_file():
        raise ValueError('Installed depends toolchain is missing')
    packages = selected['packages'].split()
    names = [f'{p}_{suffix}' for p in packages for suffix in
             ('version', 'source', 'sha256_hash', 'download_path', 'download_file', 'extra_sources')]
    values = variables(names, packages)
    result = {}
    for package in packages:
        source = Path(values[f'{package}_source'])
        checksum = values[f'{package}_sha256_hash']
        url = values[f'{package}_download_path'].rstrip('/') + '/' + values[f'{package}_download_file']
        files = source_notices(source, checksum, destination / package)
        # Qt translations/tools are separately fetched and bundled by the recipe.
        if package == 'qt':
            extras = variables(['qt_qttranslations_file_name', 'qt_qttranslations_sha256_hash',
                                'qt_qttools_file_name', 'qt_qttools_sha256_hash'], ['qt'])
            for part in ('qttranslations', 'qttools'):
                filename = extras[f'qt_{part}_file_name']
                extra_hash = extras[f'qt_{part}_sha256_hash']
                extra = Path(selected['SOURCES_PATH']) / filename
                files += source_notices(extra, extra_hash,
                                        destination / package)
        record = {'version': values[f'{package}_version'], 'source': url,
                  'archive_sha256': checksum, 'host': host,
                  'recipe': (depends / 'packages' / f'{package}.mk').read_text(),
                  'selection': selected, 'source_commit': run('git', 'rev-parse', 'HEAD^{commit}')}
        result[package] = write_record(destination, package, record, files, 'pinned-source-archive')
    return result


def brew_notices(destination, bundle=None):
    requested = ['qt@6', 'boost', 'libevent', 'zeromq', 'qrencode', 'miniupnpc', 'capnp']
    dependencies = run('brew', 'deps', '--installed', '--union', *requested).splitlines()
    names = sorted(set(requested + dependencies))
    formulae = [formula for name in names for formula in json.loads(run('brew', 'info', '--json=v2', name))['formulae']]
    required = notices.required_components('darwin-arm64') - {'sqlite'}
    seen = set()
    linkage = {}
    qt_sources = {'qtbase'}
    if bundle is not None:
        for binary in sorted(bundle.rglob('*')):
            if not binary.is_file() or binary.is_symlink():
                continue
            with binary.open('rb') as stream:
                magic = stream.read(4)
            if magic in {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}:
                linkage[str(binary.relative_to(bundle))] = {'sha256': sha256(binary), 'otool': run('otool', '-L', str(binary))}
        modules = {'Qml': 'qtdeclarative', 'Quick': 'qtdeclarative', 'QmlModels': 'qtdeclarative',
                   'QmlWorkerScript': 'qtdeclarative', 'QuickWidgets': 'qtdeclarative',
                   'Svg': 'qtsvg', 'SvgWidgets': 'qtsvg', 'ShaderTools': 'qtshadertools',
                   'Core': 'qtbase', 'Gui': 'qtbase', 'Widgets': 'qtbase', 'Network': 'qtbase',
                   'DBus': 'qtbase', 'OpenGL': 'qtbase', 'OpenGLWidgets': 'qtbase', 'PrintSupport': 'qtbase'}
        for entry in linkage.values():
            for module in re.findall(r'Qt([A-Za-z0-9]+)\.framework/', entry['otool']):
                if module not in modules:
                    raise ValueError(f'Unmapped shipped Qt framework: {module}')
                qt_sources.add(modules[module])
        if not linkage or not any('/usr/lib/libsqlite3' in entry['otool'] for entry in linkage.values()):
            raise ValueError('Built macOS bundle does not establish system SQLite linkage')
    result = {}
    for formula in formulae:
        name = formula['name']
        component = {'qtbase': 'qt', 'openssl@3': 'openssl'}.get(name, 'icu' if name.startswith('icu4c') else name)
        # Homebrew's qt umbrella uses a checksums text file, and its CA bundle
        # uses PEM. Only selected runtime notices and Qt module sources are tar.
        if name in {'qt', 'qt@6'} or (component not in required and name not in qt_sources and name != 'capnp'):
            continue
        if component in seen:
            continue
        seen.add(component)
        installed = formula['installed']
        if len(installed) != 1 or installed[0]['version'].split('_')[0] != formula['versions']['stable']:
            raise ValueError(f'Homebrew installed/source version mismatch: {name}')
        prefix = Path(run('brew', '--prefix', name))
        receipt = json.loads((prefix / 'INSTALL_RECEIPT.json').read_text())
        stable = formula['urls']['stable']
        directory = destination / component
        directory.mkdir(parents=True, exist_ok=True)
        source = acquire(stable['url'], stable['checksum'], directory / 'source.tar')
        files = source_notices(source, stable['checksum'], directory / 'licenses')
        record = {'version': installed[0]['version'], 'source': stable['url'],
                  'archive_sha256': stable['checksum'], 'formula': formula, 'installed_receipt': receipt,
                  'bundle_linkage': linkage, 'architecture': run('uname', '-m'),
                  'source_commit': run('git', 'rev-parse', 'HEAD^{commit}')}
        result[component] = write_record(destination, component, record, files, 'pinned-source-archive')
    # Core's macOS build uses the SDK/system SQLite rather than a brewed library.
    record = {'version': 'system library, macOS SDK ' + run('xcrun', '--show-sdk-version'), 'source': 'macOS SDK system SQLite',
              'sdk_path': run('xcrun', '--show-sdk-path'), 'os': run('sw_vers'), 'bundle_linkage': linkage}
    result['sqlite'] = write_record(destination, 'sqlite', record, [])
    result['sqlite']['role'] = 'system-library'
    return result


def vcpkg_notices(installed, destination):
    status = (installed / 'vcpkg/status').read_text()
    groups = {}
    for paragraph in re.split(r'\n\s*\n', status):
        fields = dict(line.split(': ', 1) for line in paragraph.splitlines() if ': ' in line)
        if fields.get('Status') != 'install ok installed' or 'Version' not in fields:
            continue
        name = fields['Package']
        if fields.get('Architecture') != 'x64-windows-static':
            continue
        component = 'boost' if name.startswith('boost-') else {'qtbase': 'qt', 'sqlite3': 'sqlite', 'libqrencode': 'qrencode'}.get(name, name)
        copyright = installed / fields['Architecture'] / 'share' / name / 'copyright'
        if not copyright.is_file() or not copyright.read_bytes():
            raise ValueError(f'Installed vcpkg copyright missing: {name}')
        ports = groups.setdefault(component, [])
        if any(entry[0]['Package'] == name for entry in ports):
            raise ValueError(f'Duplicate installed vcpkg port: {name}')
        ports.append((fields, copyright))
    result = {}
    baseline = json.loads((notices.ROOT / 'vcpkg.json').read_text())['builtin-baseline']
    for component, ports in groups.items():
        versions = sorted(set(fields['Version'] for fields, _ in ports))
        if len(versions) != 1:
            raise ValueError(f'Mismatched installed component versions: {component}')
        record = {'version': versions[0], 'source': f'vcpkg baseline {baseline}: {component}',
                  'baseline': baseline, 'installed_ports': [fields for fields, _ in ports],
                  'vcpkg_tool_commit': run('git', '-C', os.environ['VCPKG_INSTALLATION_ROOT'], 'rev-parse', 'HEAD') if os.environ.get('VCPKG_INSTALLATION_ROOT') else 'fixture',
                  'copyright_sha256': {fields['Package']: sha256(path) for fields, path in ports},
                  'source_commit': run('git', 'rev-parse', 'HEAD^{commit}')}
        result[component] = write_record(destination, component, record, [path for _, path in ports])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=sorted(notices.PLATFORMS))
    parser.add_argument('destination', type=Path)
    parser.add_argument('--depends', type=Path)
    parser.add_argument('--host')
    parser.add_argument('--vcpkg-installed', type=Path)
    parser.add_argument('--bundle', type=Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=False)
    inputs = args.destination / 'inputs'
    inputs.mkdir()
    if args.platform.startswith('linux'):
        if not args.depends or not args.host:
            parser.error('Linux requires installed depends and host')
        descriptor = depends_notices(args.depends, args.host, inputs)
    elif args.platform.startswith('darwin'):
        if not args.bundle or not args.bundle.is_dir():
            parser.error('macOS requires the actual built application bundle')
        if run('uname', '-m') != args.platform.removeprefix('darwin-'):
            parser.error('macOS runner architecture differs from selected platform')
        descriptor = brew_notices(inputs, args.bundle)
    else:
        if not args.vcpkg_installed:
            parser.error('Windows requires vcpkg installed directory')
        descriptor = vcpkg_notices(args.vcpkg_installed, inputs)
    path = args.destination / 'descriptor.json'
    path.write_text(json.dumps(descriptor, indent=2, sort_keys=True) + '\n')
    notices.collect(args.platform, path, args.destination / 'notices')
    print(args.destination / 'notices/index.json')


if __name__ == '__main__':
    main()
