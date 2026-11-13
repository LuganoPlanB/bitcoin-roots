#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Copyright (C) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the standalone legacy exporter with synthetic BDB wallets only."""

import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import signal
import stat
import struct
import subprocess
import time
from contextlib import contextmanager
from dataclasses import replace

from test_framework.address import byte_to_base58
from test_framework.extendedkey import ExtendedPrivateKey
from test_framework.legacy_export import bdb_bytes, encrypted_key, FixtureKey, fixtures, pool_key, record_key
from test_framework.messages import deser_compact_size, deser_string, hash256, ser_string
from test_framework.script import hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class ToolWalletLegacyExportTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
        self.setup_clean_chain = True

    def setup_network(self):
        pass

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_wallet_tool()

    def tool(self, args, cwd=None, data=b''):
        return subprocess.run(self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + args,
                              input=data, capture_output=True, cwd=cwd, timeout=30)

    def invocation(self, source, destination, chain='regtest'):
        return [f'-chain={chain}', f'-wallet={source}', f'-dumpfile={destination}']

    @staticmethod
    def wif(key, chain):
        return byte_to_base58(key.secret + (b'\x01' if key.compressed else b''), 128 if chain == 'main' else 239)

    def assert_failure(self, args, category, canary=None):
        result = self.tool(args)
        assert_equal(result.returncode, 1)
        assert_equal(result.stdout, b'')
        assert category.encode() in result.stderr, result.stderr
        if canary is not None:
            assert canary.encode() not in result.stderr, result.stderr
        return result

    def test_arguments(self):
        self.log.info('Reject invalid arguments before source access; redact rejected values')
        canary = 'SYNTHETIC_ARG_SECRET_658c59'
        missing = self.root / canary
        destination = self.output / 'invalid.keys'
        valid = self.invocation(missing, destination)
        cases = [
            ([], 'requires explicit'),
            (valid[:1], 'requires explicit'),
            ([arg for arg in valid if not arg.startswith('-chain=')], 'requires explicit'),
            ([arg for arg in valid if not arg.startswith('-dumpfile=')], 'requires explicit'),
            (valid + ['-wallet='], 'requires nonempty'),
            (valid + ['-dumpfile='], 'requires nonempty'),
            (valid + ['-chain='], 'requires nonempty'),
            (valid + ['-wallet'], 'requires nonempty'),
            (valid + ['-dumpfile'], 'requires nonempty'),
            (valid + ['-chain'], 'requires nonempty'),
            (valid + [f'-chain={canary}'], 'Unsupported -chain'),
            (valid + [f'-wallet={canary}'], 'Duplicate option'),
            (valid + [f'-dumpfile={canary}'], 'Duplicate option'),
            (valid + ['-chain=regtest'], 'Duplicate option'),
            (valid + [f'-unknown={canary}'], 'Unsupported option'),
            (valid + [f'-password={canary}'], 'Unsupported option'),
            (valid + [f'-stdinwalletpassphrase={canary}'], 'bare switch'),
            (valid + ['-stdinwalletpassphrase', '-stdinwalletpassphrase'], 'Duplicate option'),
            (valid + [f'-nostdinwalletpassphrase={canary}'], 'Unsupported option'),
            (valid + [f'-nowallet={canary}'], 'Unsupported option'),
            (valid + [f'-nodumpfile={canary}'], 'Unsupported option'),
            (valid + [f'-nochain={canary}'], 'Unsupported option'),
            (valid + ['-regtest'], 'Unsupported option'),
            (valid + ['-testnet'], 'Unsupported option'),
            (valid + ['-testnet4'], 'Unsupported option'),
            (valid + ['-signet'], 'Unsupported option'),
            (valid + [f'-signetchallenge={canary}'], 'Unsupported option'),
            (valid + [f'-vbparams={canary}'], 'Unsupported option'),
            (valid + [f'-testactivationheight={canary}'], 'Unsupported option'),
            (valid + [f'-regtest.wallet={canary}'], 'Unsupported option'),
            (valid + [f'-nodebug={canary}'], 'Invalid negated generic'),
            (valid + [f'-noprinttoconsole={canary}'], 'Invalid negated generic'),
            (valid + ['--wallet=alias'], 'Unsupported option'),
            (valid + ['-dumpfile=-'], 'never'),
        ]
        for args, category in cases:
            self.assert_failure(args + ['dumpwallet'], category, canary)
            assert not destination.exists()
        for args in (valid + ['dumpwallet', canary], valid + [canary, 'dumpwallet'],
                     valid + [f'dumpwallet={canary}'], valid + ['dumpwallet', f'-unknown={canary}']):
            self.assert_failure(args, 'positional', canary)
        self.assert_failure([f'-datadir={missing}'] + valid + ['dumpwallet'], 'Duplicate option', canary)
        # A single invalid datadir must also suppress its supplied value.
        result = subprocess.run(self.get_binaries().wallet_argv() + [f'-datadir={missing}'] + valid + ['dumpwallet'],
                                capture_output=True, timeout=30)
        assert_equal(result.returncode, 1)
        assert b'data directory' in result.stderr
        assert canary.encode() not in result.stderr
        for help_option in ('-help', '-h', '-?', '--help', '-version'):
            result = self.tool(['-chain=regtest', help_option, 'dumpwallet'])
            assert_equal(result.returncode, 0)
            assert_equal(result.stderr, b'')
        assert_equal(list(self.output.iterdir()), [])

    def test_plaintext(self):
        self.log.info('Export plaintext synthetic fixtures under each explicit encoding network')
        fixture = next(f for f in fixtures() if f.name == 'hd_split')
        source_dir = self.root / 'source'
        source_dir.mkdir(mode=0o700)
        source = source_dir / 'wallet.dat'
        source.write_bytes(bdb_bytes(fixture.records))
        for chain in ('main', 'test', 'testnet4', 'signet', 'regtest'):
            destination = self.output / f'{chain}.keys'
            result = self.tool(self.invocation(source, destination, chain) + ['dumpwallet'])
            assert result.returncode == 0, result.stderr
            assert_equal(result.stderr, b'')
            payload = destination.read_text(encoding='utf8')
            assert f'# roots-network: {chain}\n' in payload
            for key in fixture.keys:
                assert self.wif(key, chain) + ' ' in payload
            for seed, _ in fixture.seeds:
                root = ExtendedPrivateKey.from_seed(seed.secret).to_string(mainnet=chain == 'main')
                assert f'# extended private masterkey: {root}\n' in payload
            metadata = [json.loads(line.removeprefix('# roots-meta-v1 '))
                        for line in payload.splitlines() if line.startswith('# roots-meta-v1 ')]
            assert_equal(metadata[0]['state'], 'private')
            assert_equal(metadata[0]['counts']['private_keys'], len(fixture.keys))
            assert_equal(metadata[0]['counts']['roots'], len(fixture.seeds))
            assert payload.endswith('# roots-export-complete: 1\n')
            # No private material reaches either diagnostic stream.
            for key in fixture.keys:
                assert self.wif(key, chain).encode() not in result.stdout
        relative = self.tool(self.invocation('source', 'output/relative.keys') + ['dumpwallet'], cwd=self.root)
        assert_equal(relative.returncode, 0)
        assert_equal(relative.stderr, b'')
        assert_equal((self.output / 'relative.keys').read_bytes(), (self.output / 'regtest.keys').read_bytes())

    def test_invalid_sources(self):
        self.log.info('Reject unsupported source formats and records without publishing output')
        for name, data in [('sqlite', b'SQLite format 3\x00' + b'\x00' * 8192),
                           ('not_bdb', b'\x00' * 8192),
                           ('unknown_family', bdb_bytes([(record_key('unsupported'), b'')]))]:
            source = self.root / name
            source.write_bytes(data)
            destination = self.output / f'{name}.keys'
            category = 'Unsupported legacy wallet record family' if name == 'unknown_family' else 'supported BDB wallet'
            self.assert_failure(self.invocation(source, destination) + ['dumpwallet'], category)
            assert not destination.exists()
        self.assert_failure(self.invocation(self.root / 'absent', self.output / 'absent.keys') + ['dumpwallet'], 'source')

    def test_historical_storage_matrix(self):
        self.log.info('Compare exact synthetic historical inventories across 160 BDB geometries and two overflow cases')

        def byte_value(value):
            try:
                return {'encoding': 'utf8', 'value': value.decode('utf8')}
            except UnicodeDecodeError:
                return {'encoding': 'hex', 'value': value.hex()}

        def read_metadata(value):
            # Independent wire reader: donor CKeyMetadata versions 1/10/12.
            stream = BytesIO(value)
            version, created = struct.unpack('<iq', stream.read(12))
            path = seed_id = origin = None
            if version >= 10:
                path = byte_value(deser_string(stream))
                seed = stream.read(20)
                seed_id = seed.hex() if any(seed) else None
            if version >= 12:
                fingerprint = stream.read(4).hex()
                indexes = [struct.unpack('<I', stream.read(4))[0] for _ in range(deser_compact_size(stream))]
                origin = {'has_origin': bool(stream.read(1)[0]), 'fingerprint': fingerprint, 'path': indexes}
            assert_equal(stream.read(), b'')
            return {'version': version, 'created': str(created), 'hd_path': path, 'hd_seed_id': seed_id, 'origin': origin}

        def check(fixture, records, destination, size, order):
            source = self.inputs / 'historical-matrix.dat'
            source.write_bytes(bdb_bytes(records, page_size=size, byteorder=order))
            before = self.source_snapshot(self.inputs)
            options = ['-stdinwalletpassphrase'] if fixture.passphrase is not None else []
            result = self.tool(self.invocation(source, destination) + options + ['dumpwallet'],
                               data=fixture.passphrase + b'\n' if fixture.passphrase is not None else b'')
            assert_equal(result.returncode, 0)
            assert_equal(result.stderr, b'')
            assert_equal(self.source_snapshot(self.inputs), before)
            lines = destination.read_text(encoding='utf8').splitlines()
            payload = [line.split() for line in lines if line and not line.startswith('#')]
            objects = [json.loads(line[16:]) for line in lines if line.startswith('# roots-meta-v1 ')]
            wallet = objects[0]
            assert_equal((wallet['page_size'], wallet['byte_order'], wallet['source_bytes']), (size, order, source.stat().st_size))
            keys = {obj['pubkey']: obj for obj in objects if obj['type'] == 'key'}
            seeds = [obj for obj in objects if obj['type'] == 'seed']
            scripts = {obj['script_hex'] for obj in objects if obj['type'] == 'script'}
            assert_equal({line[0] for line in payload if line[2] != 'script=1'}, {key.wif for key in fixture.keys})
            assert_equal({pub for pub, obj in keys.items() if obj['has_private_key']}, {key.pubkey.hex() for key in fixture.keys})
            expected_roots = {(seed.key_id.hex(), seed.pubkey.hex(), active,
                               ExtendedPrivateKey.from_seed(seed.secret).to_string(mainnet=False)) for seed, active in fixture.seeds}
            assert_equal({(obj['seed_id'], obj['pubkey'], obj['active'], obj['xprv']) for obj in seeds}, expected_roots)
            assert_equal({obj['has_secret'] for obj in seeds}, {True} if seeds else set())
            assert_equal(scripts, {script.hex() for script in fixture.scripts})
            active = [root for _, _, is_active, root in expected_roots if is_active]
            assert_equal([line[30:] for line in lines if line.startswith('# extended private masterkey: ')], active)
            redeem = set()
            for raw_key, value in records:
                key_stream = BytesIO(raw_key)
                family = deser_string(key_stream)
                if family in (b'keymeta', b'watchmeta'):
                    identity = deser_string(key_stream)
                    obj = keys[identity.hex()] if family == b'keymeta' else next(obj for obj in objects if obj['type'] == 'script' and obj['script_hex'] == identity.hex())
                    assert_equal(obj['metadata'], read_metadata(value))
                elif family == b'hdchain':
                    version, external = struct.unpack('<iI', value[:8])
                    obj = next(obj for obj in objects if obj['type'] == 'hdchain')
                    assert_equal(obj, {'type': 'hdchain', 'version': version, 'seed_id': value[8:28].hex(),
                                       'external_counter': external, 'internal_counter': struct.unpack('<I', value[28:])[0] if version == 2 else None})
                elif family == b'pool':
                    index = struct.unpack('<q', key_stream.read())[0]
                    stream = BytesIO(value)
                    version, timestamp = struct.unpack('<iq', stream.read(12))
                    pubkey = deser_string(stream).hex()
                    flags = stream.read()
                    obj = next(obj for obj in objects if obj['type'] == 'pool' and obj['index'] == str(index))
                    assert_equal(obj, {'type': 'pool', 'index': str(index), 'stream_version': version, 'pubkey': pubkey,
                                       'time': str(timestamp), 'internal': bool(flags[0]) if flags else False,
                                       'internal_present': bool(flags), 'pre_split': bool(flags[1]) if len(flags) > 1 else False,
                                       'pre_split_present': len(flags) > 1})
                elif family == b'wkey':
                    pubkey = deser_string(key_stream).hex()
                    stream = BytesIO(value)
                    version = struct.unpack('<i', stream.read(4))[0]
                    deser_string(stream)
                    created, expires = struct.unpack('<qq', stream.read(16))
                    assert_equal(keys[pubkey]['wkey'], {'stream_version': version, 'created': str(created),
                                                      'expires': str(expires), 'comment': byte_value(deser_string(stream))})
                elif family == b'cscript':
                    script = deser_string(BytesIO(value))
                    assert_equal(key_stream.read(), hash160(script))
                    if script:
                        redeem.add(script.hex())
            assert_equal({line[0] for line in payload if line[2] == 'script=1'}, redeem)
            assert_equal(wallet['counts']['records'], len(records))
            assert_equal(wallet['counts']['private_keys'], len(fixture.keys))
            assert_equal(wallet['counts']['roots'], len(fixture.seeds))
            # All metadata and payload bytes must be invariant under geometry.
            return [line for line in lines[3:] if not line.startswith('# roots-meta-v1 {"type":"wallet"')]

        executed = 0
        for fixture in fixtures():
            baseline = None
            for size in (512, 1024, 2048, 4096, 8192, 16384, 32768, 65536):
                for order in ('little', 'big'):
                    destination = self.output / f'matrix-{fixture.name}-{size}-{order}.keys'
                    lines = check(fixture, fixture.records, destination, size, order)
                    if baseline is None:
                        baseline = lines
                    else:
                        assert_equal(lines, baseline)
                    executed += 1
        # Exercise a real multi-page script, not padding in an unused page.
        fixture = next(f for f in fixtures() if f.name == 'plain_mixed')
        script = b'\x61' * 12000
        records = fixture.records + ((record_key('cscript', hash160(script)), ser_string(script)),)
        fixture = replace(fixture, scripts=fixture.scripts + (script,))
        for order in ('little', 'big'):
            check(fixture, records, self.output / f'matrix-overflow-{order}.keys', 512, order)
            executed += 1
        assert_equal(executed, 162)
        self.log.info('Historical synthetic matrix: 162 passed; exact keys, roots, scripts, metadata and unchanged sources')

    def encrypted_source(self, vector, name=None, iterations=25000):
        key = FixtureKey((1).to_bytes(32, 'big'))
        master = ser_string(bytes.fromhex(vector['master_ciphertext'])) + ser_string(bytes.fromhex('0000deadbeef0000'))
        master += struct.pack('<II', 0, iterations) + ser_string(b'')
        records = [(record_key('mkey', struct.pack('<I', 1)), master), encrypted_key(key, 'imported')]
        source = self.inputs / (name or vector['name'])
        source.write_bytes(bdb_bytes(records))
        return source

    @staticmethod
    def assert_no_secret(result, passphrase):
        # Avoid displaying a fixture needle, even when the assertion fails.
        if len(passphrase) >= 8:
            assert passphrase not in result.stdout + result.stderr, 'Passphrase appeared in diagnostics'

    def test_stdin_bytes(self):
        self.log.info('Verify independently encrypted passphrase byte vectors through LF/CRLF/EOF')
        expected = None
        for vector in self.vectors:
            source = self.encrypted_source(vector)
            passphrase = bytes.fromhex(vector['passphrase_hex'])
            for suffix, terminator in [('lf', b'\n'), ('crlf', b'\r\n'), ('eof', b'')]:
                if (not passphrase and not terminator) or (passphrase.endswith(b'\r') and terminator == b'\n'):
                    continue  # EOF before any byte is missing; final CR+LF is a terminator.
                destination = self.output / f"stdin-{vector['name']}-{suffix}.keys"
                result = self.tool(self.invocation(source, destination) + ['-stdinwalletpassphrase', 'dumpwallet'], data=passphrase + terminator)
                assert result.returncode == 0, 'Byte-vector export failed'
                assert_equal(result.stderr, b'')
                self.assert_no_secret(result, passphrase)
                payload = destination.read_bytes()
                if expected is None:
                    expected = payload
                assert_equal(payload, expected)
        self.expected_encrypted = expected

    def test_stdin_errors(self):
        self.log.info('Reject missing, wrong, oversized and already supplied trailing input')
        source = self.encrypted_source(self.vectors[0], 'stdin-errors')
        for index, (data, category) in enumerate([
                (b'', b'Missing legacy passphrase input'),
                (b'\n', b'Legacy decryption failed'),
                (b'wrong\n', b'Legacy decryption failed'),
                (b'test\x00suffix\n', b'Legacy decryption failed'),
                (b'test\nextra', b'Trailing legacy passphrase input'),
                (b'test\r\n\n', b'Trailing legacy passphrase input'),
                (b'X' * 4097 + b'\n', b'4096-byte limit'),
                (b'X' * 4096 + b'\r', b'4096-byte limit'),
                (b'X' * 4095 + 'é'.encode() + b'\n', b'4096-byte limit')]):
            destination = self.output / f'stdin-error-{index}.keys'
            result = self.tool(self.invocation(source, destination) + ['-stdinwalletpassphrase', 'dumpwallet'], data=data)
            assert_equal(result.returncode, 1)
            assert_equal(result.stdout, b'')
            assert category in result.stderr, 'Wrong input failure category'
            assert not destination.exists()
        result = subprocess.run(self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, self.output / 'no-tty.keys') + ['dumpwallet'],
                                input=b'test\n', capture_output=True, start_new_session=True, timeout=10)
        assert_equal(result.returncode, 1)
        assert b'controlling terminal' in result.stderr
        self.assert_no_secret(result, b'test')

    def test_live_pipe(self):
        self.log.info('Do not wait for a second byte after a line in a live pipe')
        source = self.encrypted_source(self.vectors[0], 'live-pipe')
        for index, data in enumerate((b'test\n', b'test\r\n', b'test\nextra')):
            read_end, write_end = os.pipe()
            process = None
            try:
                # Supply the complete line/trailing bytes before startup. Keep
                # the writer open until the child has exited, so EOF cannot
                # accidentally satisfy the nonblocking-completion assertion.
                assert_equal(os.write(write_end, data), len(data))
                destination = self.output / f'live-pipe-{index}.keys'
                args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination) + ['-stdinwalletpassphrase', 'dumpwallet']
                process = subprocess.Popen(args, stdin=read_end, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                stdout, stderr = process.communicate(timeout=10)
                assert_equal(process.returncode, 1 if index == 2 else 0)
                if index == 2:
                    assert_equal(stdout, b'')
                    assert b'Trailing legacy passphrase input' in stderr
                    assert not destination.exists()
                else:
                    assert_equal(stderr, b'')
            finally:
                if process is not None:
                    if process.poll() is None:
                        process.kill()
                    process.communicate(timeout=10)
                os.close(read_end)
                os.close(write_end)

    @contextmanager
    def terminal_process(self, args, echo=True):
        import fcntl
        import pty
        import termios
        master, slave = pty.openpty()
        saved = termios.tcgetattr(slave)
        if not echo:
            saved[3] &= ~termios.ECHO
            termios.tcsetattr(slave, termios.TCSANOW, saved)
        process = None
        try:
            # Attach a controlling terminal while stdin/stdout/stderr are
            # redirected. The helper must open /dev/tty independently.
            process = subprocess.Popen(self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + args,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True, pass_fds=(slave,),
                                       preexec_fn=lambda: fcntl.ioctl(slave, termios.TIOCSCTTY, 0))
            os.set_blocking(master, False)
            yield process, master, slave, saved
        finally:
            if process is not None:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=10)
            os.close(master)
            os.close(slave)

    def wait_prompt(self, process, master, slave):
        import select
        import termios
        received = bytearray()
        deadline = time.monotonic() + 10
        while b'Wallet passphrase' not in received:
            assert process.poll() is None, 'Process exited before prompt'
            assert time.monotonic() < deadline, 'No terminal prompt'
            if select.select([master], [], [], 0.05)[0]:
                received.extend(os.read(master, 4096))
        mode = termios.tcgetattr(slave)
        assert not mode[3] & (termios.ECHO | termios.ICANON), 'Terminal remained echoing/canonical'
        # Inspect the actual running argv where the OS exposes it; never
        # display it or any input needle on an assertion failure.
        proc_args = Path(f'/proc/{process.pid}/cmdline')
        if proc_args.exists():
            observed = proc_args.read_bytes().rstrip(b'\x00').split(b'\x00')
            assert observed == [os.fsencode(arg) for arg in process.args], 'Actual argv differs from nonsecret invocation'
        return received

    @staticmethod
    def terminal_send(master, data):
        import select
        deadline = time.monotonic() + 10
        while data:
            assert time.monotonic() < deadline, 'Terminal input write timed out'
            if select.select([], [master], [], 0.05)[1]:
                size = os.write(master, data)
                data = data[size:]

    def test_terminal(self):
        if os.name != 'posix':
            self.log.info('POSIX PTY coverage unavailable; native Windows console qualification remains required')
            return
        import select
        import termios
        self.log.info('Verify controlling-terminal byte parity, no echo and exact settings restoration')
        for vector in self.vectors:
            source = self.encrypted_source(vector, 'tty-' + vector['name'])
            destination = self.output / f"tty-{vector['name']}.keys"
            data = bytes.fromhex(vector['passphrase_hex'])
            with self.terminal_process(self.invocation(source, destination) + ['dumpwallet'], echo=vector['name'] != 'empty') as (process, master, slave, saved):
                received = self.wait_prompt(process, master, slave)
                actual_args = Path(f'/proc/{process.pid}/cmdline')
                if actual_args.exists() and len(data) >= 8:
                    assert data not in actual_args.read_bytes(), 'Input present in actual argv'
                self.terminal_send(master, data + b'\r\n')
                stdout, stderr = process.communicate(timeout=20)
                assert_equal(process.returncode, 0)
                assert_equal(stderr, b'')
                assert_equal(termios.tcgetattr(slave), saved)
                while select.select([master], [], [], 0)[0]:
                    received.extend(os.read(master, 8192))
                if data:
                    assert data not in received, 'Terminal input was echoed'
                if len(data) >= 8:
                    assert data not in stdout + stderr, 'Terminal input was logged'
                assert_equal(destination.read_bytes(), self.expected_encrypted)
        for index, action in enumerate(('wrong', 'oversize', 'sigint', 'sigterm')):
            source = self.encrypted_source(self.vectors[0], f'tty-error-{index}')
            destination = self.output / f'tty-error-{index}.keys'
            with self.terminal_process(self.invocation(source, destination) + ['dumpwallet']) as (process, master, slave, saved):
                self.wait_prompt(process, master, slave)
                if action == 'wrong':
                    self.terminal_send(master, b'wrong\n')
                elif action == 'oversize':
                    self.terminal_send(master, b'X' * 4097 + b'\n')
                else:
                    process.send_signal(signal.SIGINT if action == 'sigint' else signal.SIGTERM)
                stdout, stderr = process.communicate(timeout=10)
                assert_equal(process.returncode, 1)
                assert_equal(stdout, b'')
                assert b'interrupted' in stderr if action.startswith('sig') else b'Error:' in stderr
                assert_equal(termios.tcgetattr(slave), saved)
                assert not destination.exists()

    def test_no_passphrase_needed(self):
        self.log.info('No terminal needed for plaintext/watch-only/empty/mkey-only; reject stdin switch')
        for fixture in fixtures():
            if fixture.name not in ('plain_mixed', 'watch_only', 'empty', 'mkey_only'):
                continue
            source = self.inputs / ('no-input-' + fixture.name)
            source.write_bytes(bdb_bytes(fixture.records))
            destination = self.output / f'no-input-{fixture.name}.keys'
            args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination)
            result = subprocess.run(args + ['dumpwallet'], input=b'', capture_output=True, start_new_session=True, timeout=10)
            assert_equal(result.returncode, 0)
            assert_equal(result.stderr, b'')
            destination = self.output / f'reject-input-{fixture.name}.keys'
            result = self.tool(self.invocation(source, destination) + ['-stdinwalletpassphrase', 'dumpwallet'], data=b'test\n')
            assert_equal(result.returncode, 1)
            assert b'requires encrypted private keys' in result.stderr
            assert not destination.exists()

    def test_pre_prompt_rejection(self):
        if os.name != 'posix':
            return
        import select
        import termios
        self.log.info('Reject invalid destinations and source formats before terminal prompting')
        source = self.encrypted_source(self.vectors[0], 'pre-prompt')
        existing = self.output / 'preexisting.keys'
        existing.write_bytes(b'USER FILE')
        unsupported = self.inputs / 'pre-prompt-sqlite'
        unsupported.write_bytes(b'SQLite format 3\x00' + b'\x00' * 8192)
        for input_path, destination, extra in [
                (source, existing, []),
                (source, self.root / 'absent-parent/out.keys', []),
                (source, self.inputs / 'inside-source.keys', []),
                (unsupported, self.output / 'unsupported.keys', []),
                (source, self.output / 'bad-args.keys', ['-password=SYNTHETIC_REJECTED_SECRET'])]:
            with self.terminal_process(self.invocation(input_path, destination) + extra + ['dumpwallet']) as (process, master, slave, saved):
                stdout, stderr = process.communicate(timeout=10)
                assert_equal(process.returncode, 1)
                assert_equal(stdout, b'')
                assert b'Wallet passphrase' not in stderr
                assert not select.select([master], [], [], 0)[0], 'Prompt occurred before validation'
                assert_equal(termios.tcgetattr(slave), saved)
        assert_equal(existing.read_bytes(), b'USER FILE')

    def test_processing_interruption(self):
        if os.name != 'posix':
            return
        import termios
        self.log.info('Cooperatively interrupt processing after terminal input has restored its state')
        # A maximum-budget KDF gives an observable processing interval. Its
        # ciphertext need not decrypt: cancellation takes precedence at unwind.
        source = self.encrypted_source(self.vectors[0], 'processing-interrupt', iterations=10000000)
        destination = self.output / 'processing-interrupt.keys'
        with self.terminal_process(self.invocation(source, destination) + ['dumpwallet']) as (process, master, slave, saved):
            self.wait_prompt(process, master, slave)
            self.terminal_send(master, b'test\n')
            deadline = time.monotonic() + 10
            while termios.tcgetattr(slave) != saved:
                assert process.poll() is None, 'Exited before restoring input state'
                assert time.monotonic() < deadline, 'Input state not restored'
                time.sleep(0.005)
            stat_path = Path(f'/proc/{process.pid}/stat')
            if stat_path.exists():
                def cpu_ticks():
                    fields = stat_path.read_text().rsplit(')', 1)[1].split()
                    return int(fields[11]) + int(fields[12])
                initial = cpu_ticks()
                while cpu_ticks() < initial + 5:
                    assert process.poll() is None, 'No processing interval observed'
                    assert time.monotonic() < deadline, 'No processing CPU observed'
                    time.sleep(0.005)
            else:
                time.sleep(0.2)
                assert process.poll() is None, 'No processing interval observed'
            assert not destination.exists()
            assert not list(self.output.glob('.roots-export-*'))
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=45)
            assert_equal(process.returncode, 1)
            assert_equal(stdout, b'')
            assert b'Legacy export interrupted' in stderr
            assert_equal(termios.tcgetattr(slave), saved)
            assert not destination.exists()

    @staticmethod
    def source_snapshot(directory):
        # Content/mode/mtime/siblings matter here. atime can change from both
        # exporter reads and these verification reads and is excluded.
        result = {}
        for path in [directory] + sorted(directory.rglob('*')):
            info = path.lstat()
            digest = hashlib.sha256(path.read_bytes()).hexdigest() if stat.S_ISREG(info.st_mode) else None
            result[str(path.relative_to(directory))] = (info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, digest)
        return result

    def capture_failure_diagnostics(self, case, stdout, stderr):
        artifacts = []
        streams = [('stdout', stdout), ('stderr', stderr)]
        streams += [('debug-' + str(index), path.read_bytes()) for index, path in enumerate(sorted(self.datadir.rglob('*.log')))]
        for channel, data in streams:
            path = self.failure_diagnostics / f'{case}.{channel}'
            path.write_bytes(data)
            path.chmod(0o600)
            artifacts.append({'path': str(path.relative_to(self.root)), 'bytes': len(data),
                              'sha256': hashlib.sha256(data).hexdigest()})
        assert all(canary not in data for _, data in streams for canary in self.secret_canaries), 'Synthetic secret appeared in diagnostics'
        return artifacts

    def expected_status(self, destination, metadata):
        fields = [('Records', 'records'), ('Private keys', 'private_keys'), ('Public key identities', 'public_key_identities'),
                  ('Identified seeds', 'identified_seeds'), ('Roots', 'roots'), ('Scripts', 'scripts'), ('Addresses', 'addresses'),
                  ('Reserve entries', 'reserve_entries'), ('Encryption keys', 'encryption_keys'), ('Omitted records', 'omitted_records'),
                  ('Unverified derivations', 'unverified_derivations')]
        text = f"Exported legacy wallet to {destination}\nState: {metadata['state']}\n"
        text += ''.join(f'{label}: {metadata["counts"][key]}\n' for label, key in fields)
        if metadata['counts']['private_keys']:
            text += 'Warning: The export contains unencrypted spending material. Keep it private and preserve the original backup.\n'
        return text.encode()

    def test_offline_status_and_preservation(self):
        self.log.info('Run the real CLI on readonly synthetic source trees with zero configured nodes and empty datadir')
        readonly = self.root / 'read only sources ü 空'
        readonly.mkdir(mode=0o700)
        examples = list(fixtures())
        for fixture in examples:
            directory = readonly / fixture.name
            directory.mkdir(mode=0o700)
            records = [(key, ser_string(b'SYNTHETIC_PRIVATE_LABEL_CANARY')) if key.startswith(record_key('name')) else (key, value)
                       for key, value in fixture.records]
            (directory / 'wallet.dat').write_bytes(bdb_bytes(records))
            if os.name == 'posix':
                (directory / 'wallet.dat').chmod(0o400)
                directory.chmod(0o500)
        if os.name == 'posix':
            readonly.chmod(0o500)
        before = self.source_snapshot(readonly)
        assert_equal(list(self.datadir.iterdir()), [])
        try:
            for fixture in examples:
                destination = self.output / f'offline ü 空-{fixture.name}.keys'
                options = ['-stdinwalletpassphrase'] if fixture.passphrase is not None else []
                result = self.tool(self.invocation(readonly / fixture.name, destination) + options + ['dumpwallet'],
                                   data=fixture.passphrase + b'\n' if fixture.passphrase is not None else b'')
                assert_equal(result.returncode, 0)
                assert_equal(result.stderr, b'')
                payload = destination.read_bytes()
                metadata = json.loads(next(line for line in payload.splitlines() if line.startswith(b'# roots-meta-v1 ')).removeprefix(b'# roots-meta-v1 '))
                expected_state = 'private' if fixture.keys else {'watch_only': 'watch_only', 'mkey_only': 'metadata_only', 'empty': 'empty'}[fixture.name]
                assert_equal(metadata['state'], expected_state)
                assert_equal(metadata['counts']['private_keys'], len(fixture.keys))
                assert_equal(metadata['counts']['roots'], len(fixture.seeds))
                assert_equal(result.stdout, self.expected_status(destination, metadata))
                assert_equal(result.stdout.count(b'Warning:'), int(bool(fixture.keys)))
                assert b'SYNTHETIC_PRIVATE_LABEL_CANARY' not in result.stdout + result.stderr, 'Label in status'
                for key in fixture.keys:
                    assert key.wif.encode() not in result.stdout + result.stderr, 'Private material in status'
                for seed, _ in fixture.seeds:
                    secret = ExtendedPrivateKey.from_seed(seed.secret).to_string(mainnet=False).encode()
                    assert secret not in result.stdout + result.stderr, 'Root in status'
                assert_equal(self.source_snapshot(readonly), before)
                assert_equal(list(self.datadir.iterdir()), [])  # No blocks/chainstate/RPC config/node setup.
                self.failure_results.append({'category': 'readonly_success', 'case': fixture.name,
                    'exit': result.returncode, 'unicode_spaces': True,
                    'diagnostics': self.capture_failure_diagnostics('readonly-' + fixture.name, result.stdout, result.stderr)})
        finally:
            readonly.chmod(0o700)
            for path in readonly.rglob('*'):
                path.chmod(0o700 if path.is_dir() else 0o600)

    def test_failure_preservation(self):
        self.log.info('Fail closed for source/format/record/destination errors; preserve source and existing output')
        directory = self.inputs / 'failure source ü 空'
        directory.mkdir(mode=0o700)
        fixture = next(f for f in fixtures() if f.name == 'plain_mixed')
        source = directory / 'wallet.dat'
        source.write_bytes(bdb_bytes(fixture.records))
        invalid = self.inputs / 'invalid cases ü 空'
        invalid.mkdir(mode=0o700)
        cases = [('short', b'x' * 4095), ('sqlite', b'SQLite format 3\x00' + b'\x00' * 8192),
                 ('family', bdb_bytes(fixture.records + ((record_key('SYNTHETIC_PRIVATE_RECORD_CANARY'), b'SYNTHETIC_PRIVATE_VALUE_CANARY'),))),
                 ('flags', bdb_bytes(fixture.records + ((record_key('flags'), struct.pack('<Q', 1 << 34)),)))]
        key_record = next(record for record in fixture.records if record[0].startswith(record_key('key')))
        def replace_value(records, key, value):
            return tuple((old_key, value if old_key == key else old_value) for old_key, old_value in records)
        cases.append(('truncated-key', bdb_bytes(replace_value(fixture.records, key_record[0], b'\xfd'))))
        corrupt = bytearray(key_record[1])
        corrupt[-1] ^= 1
        cases.append(('key-checksum', bdb_bytes(replace_value(fixture.records, key_record[0], bytes(corrupt)))))
        pool = pool_key(fixture.keys[0], 99, 18)
        cases.append(('pool-bool', bdb_bytes(fixture.records + ((pool[0], pool[1][:-1] + b'\x02'),))))
        malformed_script = b'\x4c\xff'
        cases.append(('script-push', bdb_bytes(fixture.records + ((record_key('cscript', hash160(malformed_script)), ser_string(malformed_script)),))))
        for page, name in [(0, 'outer-encryption'), (2, 'inner-encryption')]:
            data = bytearray(bdb_bytes(fixture.records))
            data[page * 4096 + 24] = 1  # MetaPage::encrypt_algo, not a wallet mkey.
            cases.append((name, bytes(data)))
        encrypted = next(item for item in fixtures() if item.name == 'encrypted_hd')
        ckey = next(record for record in encrypted.records if record[0].startswith(record_key('ckey')))
        ciphertext = bytearray(ckey[1][1:49])
        ciphertext[0] ^= 1
        # Keep its checksum valid so this reaches decryption/public-key proof.
        value = ser_string(bytes(ciphertext)) + hash256(bytes(ciphertext))
        cases.append(('corrupt-encrypted-key', bdb_bytes(replace_value(encrypted.records, ckey[0], value))))
        cases.append(('wrong-passphrase', bdb_bytes(encrypted.records)))
        stdin_by_name = {'corrupt-encrypted-key': b'test\n', 'wrong-passphrase': b'SYNTHETIC_WRONG_CREDENTIAL_CANARY\n'}
        for name, data in cases:
            (invalid / name).write_bytes(data)
        dirty = bytearray(bdb_bytes(fixture.records))
        struct.pack_into('<I', dirty, 0, 1)
        (invalid / 'dirty-lsn').write_bytes(dirty)
        broken_seed = next(f for f in fixtures() if f.name == 'hd_unsplit')
        (invalid / 'missing-seed').write_bytes(bdb_bytes([record for record in broken_seed.records if record[0] != record_key('key', ser_string(broken_seed.seeds[0][0].pubkey))]))
        if os.name == 'posix':
            (invalid / 'symlink').symlink_to(source)
            os.mkfifo(invalid / 'fifo')
        paths = sorted(invalid.iterdir())
        try:
            for readonly in (False, True):
                if os.name == 'posix':
                    for path in paths:
                        if not path.is_symlink():
                            path.chmod(0o400 if readonly else 0o600)
                    invalid.chmod(0o500 if readonly else 0o700)
                before = self.source_snapshot(self.inputs)
                source_snapshot = json.dumps(before, sort_keys=True).encode()
                (self.failure_diagnostics / f'source-{readonly}.json').write_bytes(source_snapshot)
                for path in paths:
                    destination = self.output / f'failure ü 空-{readonly}-{path.name}.keys'
                    outputs_before = self.source_snapshot(self.output)
                    data = stdin_by_name.get(path.name, b'')
                    options = ['-stdinwalletpassphrase'] if data else []
                    result = self.tool(self.invocation(path, destination) + options + ['-debug=1', '-printtoconsole=1', 'dumpwallet'], data=data)
                    assert_equal(result.returncode, 1)
                    assert b'clean offline copy' in result.stderr and b'original backup' in result.stderr
                    diagnostics = self.capture_failure_diagnostics(f'negative-{readonly}-{path.name}', result.stdout, result.stderr)
                    after = self.source_snapshot(self.inputs)
                    outputs_after = self.source_snapshot(self.output)
                    assert_equal(after, before)
                    assert_equal(outputs_after, outputs_before)
                    assert not destination.exists()
                    assert not list(self.output.glob('.roots-export-*'))
                    self.failure_results.append({'category': 'negative_matrix', 'case': path.name,
                        'readonly': readonly, 'exit': result.returncode, 'debug_console': True,
                        'canary_needles': len(self.secret_canaries), 'stdout_bytes': len(result.stdout), 'stderr_bytes': len(result.stderr),
                        'source_before_sha256': hashlib.sha256(source_snapshot).hexdigest(),
                        'source_after_sha256': hashlib.sha256(json.dumps(after, sort_keys=True).encode()).hexdigest(),
                        'output_before_sha256': hashlib.sha256(json.dumps(outputs_before, sort_keys=True).encode()).hexdigest(),
                        'output_after_sha256': hashlib.sha256(json.dumps(outputs_after, sort_keys=True).encode()).hexdigest(),
                        'source_unchanged': True, 'output_unchanged': True, 'temporary_remaining': 0, 'diagnostics': diagnostics})
        finally:
            invalid.chmod(0o700)
            for path in paths:
                if not path.is_symlink():
                    path.chmod(0o600)
        if os.name == 'posix' and os.geteuid() != 0:
            before = self.source_snapshot(self.inputs)
            outputs_before = self.source_snapshot(self.output)
            saved_mode = stat.S_IMODE(source.stat().st_mode)
            source.chmod(0)
            try:
                destination = self.output / 'unreadable-source.keys'
                result = self.tool(self.invocation(source, destination) + ['-debug=1', '-printtoconsole=1', 'dumpwallet'])
                assert_equal(result.returncode, 1)
                diagnostics = self.capture_failure_diagnostics('source-permission', result.stdout, result.stderr)
                assert not destination.exists()
                assert not list(self.output.glob('.roots-export-*'))
            finally:
                source.chmod(saved_mode)
            assert_equal(self.source_snapshot(self.inputs), before)
            assert_equal(self.source_snapshot(self.output), outputs_before)
            self.failure_results.append({'category': 'source_permission', 'exit': result.returncode, 'source_unchanged': True, 'diagnostics': diagnostics})
        existing = self.output / 'existing-user-file'
        existing.write_bytes(b'UNCHANGED USER FILE')
        existing_dir = self.output / 'existing-directory'
        existing_dir.mkdir(mode=0o700)
        destinations = [existing, existing_dir, source, self.root / 'missing-parent/out.keys', directory / 'inside.keys']
        if os.name == 'posix':
            linked = self.output / 'source-hardlink'
            os.link(source, linked)
            symlink = self.output / 'source-symlink'
            symlink.symlink_to(source)
            dangling = self.output / 'dangling-symlink'
            dangling.symlink_to(self.output / 'absent')
            destinations += [linked, symlink, dangling]
            parent_alias = self.root / 'output-parent-alias'
            parent_alias.symlink_to(self.output, target_is_directory=True)
            destinations.append(parent_alias / 'aliased-parent.keys')
            unsafe_parent = self.root / 'unsafe-output-parent'
            unsafe_parent.mkdir(mode=0o770)
            unsafe_parent.chmod(0o770)
            destinations.append(unsafe_parent / 'unsafe.keys')
            readonly_parent = self.root / 'readonly-output-parent'
            readonly_parent.mkdir(mode=0o500)
            if os.geteuid() != 0:
                destinations.append(readonly_parent / 'readonly.keys')
        before = self.source_snapshot(self.inputs)
        for destination in destinations:
            outputs_before = self.source_snapshot(self.output)
            present_before = os.path.lexists(destination)
            result = self.tool(self.invocation(source, destination) + ['dumpwallet'])
            assert_equal(result.returncode, 1)
            assert_equal(result.stdout, b'')
            assert b'Error:' in result.stderr
            assert_equal(self.source_snapshot(self.output), outputs_before)
            assert_equal(self.source_snapshot(self.inputs), before)
            assert_equal(os.path.lexists(destination), present_before)
            if destination.parent.is_dir():
                assert not list(destination.parent.glob('.roots-export-*'))
            self.failure_results.append({'category': 'destination_alias_permission', 'case': destination.name,
                'exit': result.returncode, 'output_unchanged': True, 'destination_presence_unchanged': True,
                'diagnostics': self.capture_failure_diagnostics('destination-' + str(len(self.failure_results)), result.stdout, result.stderr)})
        if os.name == 'posix':
            readonly_parent.chmod(0o700)  # Restore our test directory for teardown.
            unsafe_parent.chmod(0o700)
        assert_equal(existing.read_bytes(), b'UNCHANGED USER FILE')
        assert_equal(self.source_snapshot(self.inputs), before)

    def test_source_change_and_output_race(self):
        if os.name != 'posix':
            return
        import termios
        self.log.info('Detect source changes and output creation after the prompt without overwriting')
        for action in ('change', 'replace', 'destination'):
            source = self.encrypted_source(self.vectors[0], 'race-' + action)
            original = source.read_bytes()
            destination = self.output / f'race-{action}.keys'
            with self.terminal_process(self.invocation(source, destination) + ['dumpwallet']) as (process, master, slave, saved):
                self.wait_prompt(process, master, slave)
                assert process.poll() is None and not destination.exists(), 'Race gate was not held'
                if action == 'change':
                    changed = bytearray(original)
                    changed[-1] ^= 1
                    source.write_bytes(changed)
                elif action == 'replace':
                    replacement = self.inputs / 'replacement'
                    replacement.write_bytes(original)
                    replacement.replace(source)
                else:
                    destination.write_bytes(b'UNCHANGED RACING USER FILE')
                injected_source_state = self.source_snapshot(self.inputs)
                injected_output_state = self.source_snapshot(self.output)
                self.terminal_send(master, b'test\n')
                stdout, stderr = process.communicate(timeout=10)
                assert_equal(process.returncode, 1)
                assert_equal(stdout, b'')
                assert b'changed' in stderr if action != 'destination' else b'already exists' in stderr
                assert_equal(termios.tcgetattr(slave), saved)
                assert_equal(self.source_snapshot(self.inputs), injected_source_state)
                assert_equal(self.source_snapshot(self.output), injected_output_state)
                if action == 'destination':
                    assert_equal(destination.read_bytes(), b'UNCHANGED RACING USER FILE')
                else:
                    assert not destination.exists()
                assert not list(self.output.glob('.roots-export-*'))
            assert process.poll() is not None
            self.failure_results.append({'category': 'prompt_source_destination_race', 'case': action,
                'gate_observed': True, 'released': True, 'settled': True, 'exit': process.returncode,
                'source_after_injection_unchanged': True, 'output_after_injection_unchanged': True,
                'diagnostics': self.capture_failure_diagnostics('race-' + action, stdout, stderr)})

    def test_output_io_and_reporting(self):
        if os.name != 'posix':
            return
        import resource
        self.log.info('Unwind handled output failure; keep committed success for closed/broken report channels')
        fixture = next(f for f in fixtures() if f.name == 'plain_mixed')
        source = self.inputs / 'output-io-source'
        source.write_bytes(bdb_bytes(fixture.records))
        before = self.source_snapshot(self.inputs)
        destination = self.output / 'limited-output.keys'
        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (512, 512))
        result = subprocess.run(self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination) + ['dumpwallet'],
                                input=b'', capture_output=True, preexec_fn=limit_output, timeout=10)
        assert_equal(result.returncode, 1)
        assert_equal(result.stdout, b'')
        assert b'Error:' in result.stderr
        assert not destination.exists()
        assert not list(self.output.glob('.roots-export-*'))
        for channel in ('closed', 'broken'):
            destination = self.output / f'{channel}-report.keys'
            args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination) + ['dumpwallet']
            if channel == 'closed':
                result = subprocess.run(args, input=b'', capture_output=True, preexec_fn=lambda: os.close(1), timeout=10)
            else:
                read_end, write_end = os.pipe()
                os.close(read_end)
                try:
                    result = subprocess.run(args, input=b'', stdout=write_end, stderr=subprocess.PIPE, timeout=10)
                finally:
                    os.close(write_end)
            assert_equal(result.returncode, 0)
            assert b'Export was published' in result.stderr
            assert b'Error:' not in result.stderr
            assert destination.read_bytes().endswith(b'# roots-export-complete: 1\n')
        assert_equal(self.source_snapshot(self.inputs), before)

    def test_report_source_aliases(self):
        if os.name != 'posix':
            return
        self.log.info('Suppress caller-owned append report channels which alias the admitted source')
        for case in ('stdout', 'hardlink-stdout', 'stderr-error', 'hardlink-stderr-error', 'both'):
            source = self.inputs / ('report-source-' + case)
            records = [(record_key('unknown'), b'SYNTHETIC_PRIVATE_VALUE_CANARY')] if 'error' in case else []
            source.write_bytes(bdb_bytes(records))
            alias = source
            if case.startswith('hardlink'):
                alias = self.inputs / ('report-hardlink-' + case)
                os.link(source, alias)
            before = self.source_snapshot(self.inputs)
            destination = self.output / f'report-source-{case}.keys'
            args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination) + ['dumpwallet']
            with alias.open('ab', buffering=0) as channel:
                result = subprocess.run(args, input=b'', stdout=channel if 'stdout' in case or case == 'both' else subprocess.PIPE,
                                        stderr=channel if 'stderr' in case or case == 'both' else subprocess.PIPE, timeout=10)
            assert_equal(result.returncode, 1 if 'error' in case else 0)
            assert_equal(self.source_snapshot(self.inputs), before)
            if 'error' in case:
                assert_equal(result.stdout, b'')
                assert not destination.exists()
            else:
                assert destination.read_bytes().endswith(b'# roots-export-complete: 1\n')
                if case != 'both':
                    assert b'Export was published' in result.stderr
            assert not list(self.output.glob('.roots-export-*'))

        for case in ('stderr', 'hardlink-stderr', 'both'):
            source = self.inputs / ('report-destination-source-' + case)
            source.write_bytes(bdb_bytes([]))
            destination = self.output / ('existing-report-destination-' + case)
            destination.write_bytes(b'UNCHANGED EXISTING DESTINATION')
            alias = destination
            if case.startswith('hardlink'):
                alias = self.output / ('destination-report-hardlink-' + case)
                os.link(destination, alias)
            inputs_before = self.source_snapshot(self.inputs)
            outputs_before = self.source_snapshot(self.output)
            args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, destination) + ['dumpwallet']
            with alias.open('ab', buffering=0) as channel:
                result = subprocess.run(args, input=b'', stdout=channel if case == 'both' else subprocess.PIPE,
                                        stderr=channel, timeout=10)
            assert_equal(result.returncode, 1)
            assert_equal(self.source_snapshot(self.inputs), inputs_before)
            assert_equal(self.source_snapshot(self.output), outputs_before)

        source = self.inputs / 'normal-file-log-source'
        source.write_bytes(bdb_bytes([(record_key('unknown'), b'SYNTHETIC_PRIVATE_VALUE_CANARY')]))
        before = self.source_snapshot(self.inputs)
        logfile = self.root / 'ordinary-diagnostics.log'
        logfile.write_bytes(b'EXISTING USER LOG\n')
        args = self.get_binaries().wallet_argv() + [f'-datadir={self.datadir}'] + self.invocation(source, self.output / 'normal-file-log.keys')
        with logfile.open('ab', buffering=0) as channel:
            result = subprocess.run(args + ['dumpwallet'], input=b'', stdout=subprocess.PIPE, stderr=channel, timeout=10)
        assert_equal(result.returncode, 1)
        assert logfile.read_bytes().startswith(b'EXISTING USER LOG\nError: ')
        assert b'SYNTHETIC_PRIVATE_VALUE_CANARY' not in logfile.read_bytes()
        with logfile.open('ab', buffering=0) as channel:
            result = subprocess.run(args + ['-help', 'dumpwallet'], input=b'', stdout=channel, stderr=subprocess.PIPE, timeout=10)
        assert_equal(result.returncode, 0)
        assert_equal(result.stderr, b'')
        assert b'Usage: bitcoin-wallet' in logfile.read_bytes()
        assert_equal(self.source_snapshot(self.inputs), before)

    def run_test(self):
        self.failure_results = []
        self.root = Path(self.options.tmpdir) / 'legacy-export'
        self.root.mkdir(mode=0o700)
        self.failure_diagnostics = self.root / 'failure-diagnostics'
        self.failure_diagnostics.mkdir(mode=0o700)
        self.secret_canaries = [b'SYNTHETIC_PRIVATE_', b'SYNTHETIC_WRONG_CREDENTIAL_CANARY']
        for item in fixtures():
            self.secret_canaries.extend(key.wif.encode() for key in item.keys)
            self.secret_canaries.extend(ExtendedPrivateKey.from_seed(seed.secret).to_string(mainnet=False).encode() for seed, _ in item.seeds)
        self.secret_canaries = sorted(set(self.secret_canaries))
        self.datadir = self.root / 'datadir'
        self.datadir.mkdir(mode=0o700)
        self.output = self.root / 'output'
        self.output.mkdir(mode=0o700)
        self.inputs = self.root / 'inputs'
        self.inputs.mkdir(mode=0o700)
        vector_path = Path(self.config['environment']['SRCDIR']) / 'test/functional/data/legacy_export_passphrases.json'
        self.vectors = json.loads(vector_path.read_text(encoding='utf8'))['vectors']
        self.test_arguments()
        self.test_plaintext()
        self.test_historical_storage_matrix()
        self.test_invalid_sources()
        self.test_stdin_bytes()
        self.test_stdin_errors()
        self.test_live_pipe()
        self.test_terminal()
        self.test_no_passphrase_needed()
        self.test_pre_prompt_rejection()
        self.test_processing_interruption()
        self.test_offline_status_and_preservation()
        self.test_failure_preservation()
        self.test_source_change_and_output_race()
        self.test_output_io_and_reporting()
        self.test_report_source_aliases()
        assert not list(self.output.glob('.roots-export-*'))
        summary = {'checks': self.failure_results, 'executed': len(self.failure_results),
                   'failed': 0, 'atime': 'Excluded: exporter and verification reads can update atime',
                   'permission_uid': os.geteuid() if os.name == 'posix' else None,
                   'limitations': 'Native permission/PTY execution is platform-specific; native publication races are checked in legacy_wallet_export_tests'}
        (self.root / 'failure-matrix-results.json').write_text(json.dumps(summary, indent=2) + '\n')
        self.log.info('Failure and readonly matrix: %d identified checks passed; no leaked canary or unowned change', len(self.failure_results))


if __name__ == '__main__':
    ToolWalletLegacyExportTest(__file__).main()
