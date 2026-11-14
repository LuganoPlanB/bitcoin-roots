#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Copyright (C) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compare offline exports with exact historical release-produced wallets.

Requires the pinned v29.5 and v0.18.1 previous-release binaries. These cases
supplement the synthetic matrix; they do not establish other release ranges.
All donor nodes use disposable regtest directories with networking disabled.
"""

import hashlib
from io import BytesIO
import json
from pathlib import Path
import shutil
import subprocess

from test_framework.address import base58_to_byte
from test_framework.extendedkey import ExtendedPrivateKey
from test_framework.legacy_export import FixtureKey
from test_framework.messages import deser_string
from test_framework.script import hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class ToolWalletLegacyExportCompatibilityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.wallet_names = []
        # No P2P listener: suppress the framework's otherwise automatic bind.
        self.bind_to_localhost_only = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_wallet_tool()
        self.skip_if_no_previous_releases()

    def setup_network(self):
        args = ['-listen=0', '-connect=0', '-dnsseed=0', '-discover=0', '-keypool=3']
        self.add_nodes(2, versions=[290500, 180100], extra_args=[args + ['-deprecatedrpc=create_bdb'], args])
        self.start_nodes()

    @staticmethod
    def payload(path):
        lines = path.read_text(encoding='utf8').splitlines()
        records = [line.split() for line in lines if line and not line.startswith('#')]
        keys = {row[0] for row in records if row[2] != 'script=1'}
        scripts = {row[0] for row in records if row[2] == 'script=1'}
        roots = [line[30:] for line in lines if line.startswith('# extended private masterkey: ')]
        return keys, scripts, roots, records, lines

    def compare_wallet(self, node, version, name, *, encrypted=False, public_only=False, blank=False):
        self.log.info('Compare release %s wallet %s, preserving its original records before any newer load', version, name)
        wallet_dir = self.root / (name + '-original')
        options = dict(wallet_name=str(wallet_dir), disable_private_keys=public_only, blank=blank)
        if version == 290500:
            options['descriptors'] = False
        node.createwallet(**options)
        wallet = node.get_wallet_rpc(str(wallet_dir))
        addresses = []
        if public_only:
            wallet.importaddress('51', 'watch-only', False)
        elif not blank:
            addresses = [wallet.getnewaddress('', 'legacy'), wallet.getrawchangeaddress('legacy')]
            imported = FixtureKey((1).to_bytes(32, 'big'))
            wallet.importprivkey(imported.wif, 'independent imported key', False)
            public_keys = [wallet.getaddressinfo(address)['pubkey'] for address in addresses]
            wallet.addmultisigaddress(2, public_keys, 'multisig', 'legacy')
            if encrypted:
                # Encryption rotates the active seed; the old seed and all
                # unrelated imported keys must still appear in the export.
                wallet.encryptwallet('synthetic reference passphrase')
                wallet.walletpassphrase('synthetic reference passphrase', 60)
                addresses.append(wallet.getnewaddress('', 'legacy'))
        reference_metadata = [wallet.getaddressinfo(address) for address in addresses]
        wallet_info = wallet.getwalletinfo()
        donor_dump = self.root / (name + '-donor.keys')
        wallet.dumpwallet(str(donor_dump))
        node.unloadwallet(str(wallet_dir))
        source_dir = self.root / (name + '-offline')
        source_dir.mkdir(mode=0o700)
        source = source_dir / 'wallet.dat'
        shutil.copy2(wallet_dir / 'wallet.dat', source)
        before = source.read_bytes()
        # Preserve every original record before any newer reference can load
        # this copy. The established raw dump does no wallet reconstruction.
        raw_dump = self.root / (name + '-original-records.dump')
        raw_result = subprocess.run(self.get_binaries().wallet_argv() + [f'-datadir={self.empty_datadir}',
            '-regtest', f'-wallet={source_dir}', f'-dumpfile={raw_dump}', 'dump'], capture_output=True, timeout=30)
        assert_equal(raw_result.returncode, 0)
        raw_lines = raw_dump.read_text(encoding='ascii').splitlines()
        assert_equal(raw_lines[0], 'BITCOIN_CORE_WALLET_DUMP,1')
        records = [tuple(bytes.fromhex(part) for part in line.split(',')) for line in raw_lines
                   if line.split(',')[0] not in ('BITCOIN_CORE_WALLET_DUMP', 'format', 'checksum')]
        stored_scripts = set()
        for raw_key, value in records:
            stream = BytesIO(raw_key)
            if deser_string(stream) == b'cscript':
                script = deser_string(BytesIO(value))
                assert_equal(stream.read(), hash160(script))
                if script:
                    stored_scripts.add(script.hex())
        destination = self.output / (name + '.keys')
        args = self.get_binaries().wallet_argv() + [f'-datadir={self.empty_datadir}', '-chain=regtest',
            f'-wallet={source}', f'-dumpfile={destination}']
        if encrypted:
            args += ['-stdinwalletpassphrase']
        result = subprocess.run(args + ['dumpwallet'], input=b'synthetic reference passphrase\n' if encrypted else b'',
                                capture_output=True, timeout=30)
        assert_equal(result.returncode, 0)
        assert_equal(result.stderr, b'')
        assert_equal(hashlib.sha256(source.read_bytes()).digest(), hashlib.sha256(before).digest())
        assert_equal(list(source_dir.iterdir()), [source])
        actual_keys, actual_scripts, actual_roots, _, lines = self.payload(destination)
        donor_keys, donor_scripts, donor_roots, donor_records, _ = self.payload(donor_dump)
        assert_equal(actual_keys, donor_keys)
        assert_equal(actual_scripts, stored_scripts)
        # Core's live keystore synthesizes P2WPKH wrapper scripts in memory
        # for compressed keys, including ones never persisted as cscript.
        # See pinned v29.5 signingprovider.cpp::ImplicitlyLearnRelatedKeyScripts
        # and v0.18.1 keystore.cpp's corresponding helper. Offline export keeps
        # original stored scripts; it does not invent these cache entries.
        implicit_scripts = set()
        for wif in donor_keys:
            secret = base58_to_byte(wif)[0]
            if len(secret) == 33:
                implicit_scripts.add((b'\x00\x14' + hash160(FixtureKey(secret[:32]).pubkey)).hex())
        assert_equal(donor_scripts, stored_scripts | implicit_scripts)
        assert_equal(actual_roots, donor_roots)
        metadata = [json.loads(line[16:]) for line in lines if line.startswith('# roots-meta-v1 ')]
        key_rows = {obj['pubkey']: obj for obj in metadata if obj['type'] == 'key'}
        for info in reference_metadata:
            stored = key_rows[info['pubkey']]['metadata']
            assert_equal(stored['created'], str(info['timestamp']))
            # RPC displays uint160 IDs in reverse numeric order; the file
            # preserves the original serialized Hash160 byte order.
            assert_equal(stored['hd_seed_id'], bytes.fromhex(info['hdseedid'])[::-1].hex())
            if stored['origin'] is not None and stored['origin']['has_origin']:
                path = [int(index.rstrip("'h")) | (0x80000000 if index.endswith(("'", 'h')) else 0)
                        for index in info['hdkeypath'].split('/')[1:]]
                assert_equal(stored['origin']['path'], path)
                if 'hdmasterfingerprint' in info:
                    assert_equal(stored['origin']['fingerprint'], info['hdmasterfingerprint'])
            else:
                assert_equal(stored['hd_path'], {'encoding': 'utf8', 'value': info['hdkeypath']})
        seed_rows = [obj for obj in metadata if obj['type'] == 'seed']
        # Derive every active/inactive root independently from the donor's
        # seed WIFs. Do not use the exported roots as their own expectations.
        seed_wifs = [row[0] for row in donor_records if row[2] in ('hdseed=1', 'inactivehdseed=1')]
        expected_roots = {ExtendedPrivateKey.from_seed(base58_to_byte(wif)[0][:32]).to_string(mainnet=False)
                          for wif in seed_wifs}
        assert_equal({obj['xprv'] for obj in seed_rows}, expected_roots)
        assert_equal(metadata[0]['counts']['private_keys'], len(donor_keys))
        assert_equal(metadata[0]['counts']['records'], len(records))
        assert_equal(metadata[0]['counts']['reserve_entries'], wallet_info['keypoolsize'] + wallet_info.get('keypoolsize_hd_internal', 0))
        assert_equal(metadata[0]['counts']['roots'], len(expected_roots))
        if public_only or blank:
            assert_equal((len(actual_keys), len(expected_roots)), (0, 0))
        else:
            assert FixtureKey((1).to_bytes(32, 'big')).wif in actual_keys
            assert len(expected_roots) >= (2 if encrypted else 1)

    def run_test(self):
        self.root = Path(self.options.tmpdir) / 'reference-exports'
        self.root.mkdir(mode=0o700)
        self.output = self.root / 'output'
        self.output.mkdir(mode=0o700)
        self.empty_datadir = self.root / 'empty-datadir'
        self.empty_datadir.mkdir(mode=0o700)
        for node, version in zip(self.nodes, [290500, 180100]):
            assert_equal(node.getnetworkinfo()['version'], version)
            self.compare_wallet(node, version, f'{version}-plain')
            self.compare_wallet(node, version, f'{version}-rotated', encrypted=True)
        self.compare_wallet(self.nodes[0], 290500, '290500-watch', public_only=True, blank=True)
        self.compare_wallet(self.nodes[0], 290500, '290500-empty', blank=True)
        assert_equal(list(self.empty_datadir.iterdir()), [])
        self.log.info('Historical release matrix: 6 passed; complete WIF/script sets and independent active/inactive roots')


if __name__ == '__main__':
    ToolWalletLegacyExportCompatibilityTest(__file__).main()
