#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Copyright (C) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Prove exported authority independently using isolated Core 29.5/0.18.1.

Payload import, comment reconstruction, transaction discovery and signing are
separate checks. No public P2P connection or real wallet is used.
"""

from dataclasses import replace
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_framework.address import base58_to_byte, key_to_p2pkh, script_to_p2sh
from test_framework.extendedkey import ExtendedPrivateKey
from test_framework.key import ECKey, ECPubKey
from test_framework.legacy_export import bdb_bytes, fixtures, key_metadata, pool_key, record_key
from test_framework.messages import ser_string
from test_framework.script import hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, dumb_sync_blocks


class ToolWalletLegacyExportRecoveryTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.wallet_names = []
        self.bind_to_localhost_only = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_wallet_tool()
        self.skip_if_no_previous_releases()

    def setup_network(self):
        args = ['-listen=0', '-connect=0', '-dnsseed=0', '-discover=0', '-keypool=3']
        self.add_nodes(2, versions=[290500, 180100], extra_args=[args + ['-deprecatedrpc=create_bdb'], args])
        self.start_nodes()

    def blank_wallet(self, node, version, name):
        path = self.root / name
        options = dict(wallet_name=str(path), blank=True)
        if version == 290500:
            options['descriptors'] = False
        node.createwallet(**options)
        return node.get_wallet_rpc(str(path))

    def record_check(self, category, **identity):
        self.check_counts[category] += 1
        self.check_details.append({'category': category, **identity, 'result': 'passed'})

    def export(self, fixture, name):
        source = self.inputs / (name + '.dat')
        source.write_bytes(bdb_bytes(fixture.records))
        before = source.read_bytes()
        destination = self.output / (name + '.keys')
        args = self.get_binaries().wallet_argv() + [f'-datadir={self.empty_datadir}', '-chain=regtest',
            f'-wallet={source}', f'-dumpfile={destination}']
        if fixture.passphrase is not None:
            args.append('-stdinwalletpassphrase')
        result = subprocess.run(args + ['dumpwallet'], input=fixture.passphrase + b'\n' if fixture.passphrase is not None else b'',
                                capture_output=True, timeout=30)
        assert_equal(result.returncode, 0)
        assert_equal(result.stderr, b'')
        assert_equal(source.read_bytes(), before)
        return destination

    @staticmethod
    def parse(path):
        lines = path.read_text(encoding='utf8').splitlines()
        payload = [line.split() for line in lines if line and not line.startswith('#')]
        metadata = [json.loads(line[16:]) for line in lines if line.startswith('# roots-meta-v1 ')]
        recovered = {}
        for row in payload:
            if row[2] == 'script=1':
                continue
            data, version = base58_to_byte(row[0])
            assert_equal(version, 239)
            assert len(data) in (32, 33)
            if len(data) == 33:
                assert_equal(data[-1], 1)
            key = ECKey()
            key.set(data[:32], compressed=len(data) == 33)
            recovered[key.get_pubkey().get_bytes()] = (row[0], key)
        return payload, metadata, recovered

    @staticmethod
    def parse_root(encoded):
        # Independent length/version/root-header validation, then the existing
        # pure Python BIP32 implementation; no exporter decoder is involved.
        tail, first = base58_to_byte(encoded)
        wire = bytes([first]) + tail
        assert_equal(len(wire), 78)
        assert_equal(wire[:4], bytes.fromhex('04358394'))
        assert_equal(wire[4:13], b'\x00' * 9)
        assert_equal(wire[45], 0)
        key = ECKey()
        key.set(wire[46:], compressed=True)
        root = ExtendedPrivateKey(key, wire[13:45])
        assert_equal(root.to_string(mainnet=False), encoded)
        return root

    def check_independent_material(self, path, fixture):
        payload, metadata, recovered = self.parse(path)
        assert_equal(set(recovered), {key.pubkey for key in fixture.keys})
        seed_row = next(obj for obj in metadata if obj['type'] == 'seed')
        root = self.parse_root(seed_row['xprv'])
        assert_equal(seed_row['active'], True)
        roots_by_seed = {obj['seed_id']: self.parse_root(obj['xprv']) for obj in metadata if obj['type'] == 'seed'}
        key_rows = {obj['pubkey']: obj for obj in metadata if obj['type'] == 'key'}
        derived_wifs = []
        self.roles = {}
        for role, classification in [('receive', 'hd_receive'), ('change', 'hd_change')]:
            row = next(obj for obj in key_rows.values() if obj['classification'] == classification)
            parent = roots_by_seed[row['metadata']['hd_seed_id']]
            path = row['metadata']['hd_path']['value']
            origin = row['metadata']['origin']
            if origin is not None and origin['has_origin']:
                assert_equal(origin['fingerprint'], parent._fingerprint().hex())
                from_origin = 'm' + ''.join('/' + str(index & 0x7fffffff) + ("'" if index & 0x80000000 else '') for index in origin['path'])
                assert_equal(path, from_origin)
            child = parent.derive_path(path)
            pubkey = child.key.get_pubkey().get_bytes()
            assert_equal(pubkey.hex(), row['pubkey'])
            assert_equal(child.key.get_bytes(), recovered[pubkey][1].get_bytes())
            self.roles[role] = pubkey
            derived_wifs.append(recovered[pubkey][0])
        imported = next(obj for obj in key_rows.values() if obj['classification'] == 'unclassified')
        self.roles['imported'] = bytes.fromhex(imported['pubkey'])
        assert self.roles['imported'] not in [root.derive_path(path).key.get_pubkey().get_bytes()
            for path in ("m/0'/0'/0'", "m/0'/1'/0'")]
        for role, pubkey in self.roles.items():
            digest = hashlib.sha256(('offline recovery ' + role).encode()).digest()
            signature = recovered[pubkey][1].sign_ecdsa(digest, rfc6979=True)
            public = ECPubKey()
            public.set(pubkey)
            assert public.verify_ecdsa(signature, digest)
            assert not public.verify_ecdsa(signature, hashlib.sha256(digest).digest())
            self.record_check('independent_message_signatures', role=role)
        # Unknown creation times remain unknown in comments and conservatively
        # old in every payload line, including the redeem script.
        assert all(row[1] == '1970-01-01T00:00:01Z' for row in payload)
        assert all(obj['created_at'] is None for obj in key_rows.values())
        assert_equal(min(int(datetime.fromisoformat(row[1].replace('Z', '+00:00')).timestamp()) for row in payload), 1)
        return recovered, derived_wifs

    def funding_transactions(self, fixture, recovered):
        node = self.nodes[0]
        early = 1296688602 + 86400
        node.setmocktime(early)
        node.createwallet('funder', descriptors=False)
        funder = node.get_wallet_rpc('funder')
        miner = funder.getnewaddress('', 'legacy')
        self.generatetoaddress(node, 101, miner, sync_fun=self.no_op)
        assert funder.getbalance() > Decimal('4')
        scripts = {role: b'\x76\xa9\x14' + hash160(pubkey) + b'\x88\xac' for role, pubkey in self.roles.items()}
        scripts['multisig'] = b'\xa9\x14' + hash160(fixture.scripts[0]) + b'\x87'
        destinations = {key_to_p2pkh(pubkey): Decimal('1') for pubkey in self.roles.values()}
        destinations[script_to_p2sh(fixture.scripts[0])] = Decimal('1')
        txid = funder.sendmany('', destinations)
        self.generatetoaddress(node, 1, miner, sync_fun=self.no_op)
        self.early_time = node.getblockheader(node.getbestblockhash())['time']
        node.setmocktime(early + 86400)
        self.generatetoaddress(node, 1, miner, sync_fun=self.no_op)
        self.late_time = node.getblockheader(node.getbestblockhash())['time']
        assert self.late_time - self.early_time > 7200
        for other in self.nodes[1:]:
            dumb_sync_blocks(src=node, dst=other)
        decoded = node.decoderawtransaction(funder.gettransaction(txid)['hex'])
        self.spends = {}
        for role, script in scripts.items():
            output = next(row for row in decoded['vout'] if row['scriptPubKey']['hex'] == script.hex())
            prevout = {'txid': txid, 'vout': output['n'], 'scriptPubKey': script.hex(), 'amount': Decimal('1')}
            if role == 'multisig':
                prevout['redeemScript'] = fixture.scripts[0].hex()
            raw = node.createrawtransaction([{'txid': txid, 'vout': output['n']}], {miner: Decimal('0.999')})
            self.spends[role] = (raw, prevout)
        # A root supplying the recorded receive/change paths cannot sign the
        # independent imported-key output or the 2-of-2 multisig by itself.
        for role in ('imported', 'multisig'):
            raw, prevout = self.spends[role]
            incomplete = node.signrawtransactionwithkey(raw, self.root_only_wifs, [prevout])
            assert_equal(incomplete['complete'], False)
            assert incomplete['errors']
            self.record_check('root_only_incomplete', role=role)
        for role, (raw, prevout) in self.spends.items():
            wifs = [recovered[self.roles['imported']][0], recovered[self.roles['receive']][0]] if role == 'multisig' else [recovered[self.roles[role]][0]]
            signed = node.signrawtransactionwithkey(raw, wifs, [prevout])
            assert_equal(signed['complete'], True)
            assert_equal(node.testmempoolaccept([signed['hex']])[0]['allowed'], True)
            self.record_check('independent_script_acceptances', role=role)
        return txid

    def check_payload_import(self, export, recovered, txid):
        lines = export.read_text().splitlines()
        stripped = self.output / 'payload-only.keys'
        stripped.write_text('\n'.join(line for line in lines if line and not line.startswith('#')) + '\n')
        late = self.output / 'late-control.keys'
        timestamp = datetime.fromtimestamp(self.late_time, timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        late.write_text('\n'.join(' '.join([row[0], timestamp] + row[2:]) for row in
            (line.split() for line in lines if line and not line.startswith('#'))) + '\n')
        for node, version in zip(self.nodes, [290500, 180100]):
            late_wallet = self.blank_wallet(node, version, f'late-{version}')
            with node.assert_debug_log(expected_msgs=['Rescanning last 1 blocks']):
                late_wallet.importwallet(str(late))
            assert_raises_rpc_error(-5, 'Invalid or non-wallet transaction id', late_wallet.gettransaction, txid)
            self.record_check('late_scan_controls', version=version)
            for mode, path in [('full', export), ('payload', stripped)]:
                wallet = self.blank_wallet(node, version, f'{mode}-{version}')
                with node.assert_debug_log(expected_msgs=[f'Rescanning last {node.getblockcount() + 1} blocks']):
                    wallet.importwallet(str(path))
                # This old transaction predates the late control's scan window.
                assert_equal(wallet.gettransaction(txid)['txid'], txid)
                assert_equal(wallet.getbalance(), Decimal('4'))
                info = wallet.getwalletinfo()
                assert 'hdseedid' not in info
                assert_equal(info['keypoolsize'], 0)
                roundtrip = self.output / f'{mode}-{version}-imported.keys'
                wallet.dumpwallet(str(roundtrip))
                _, _, imported_keys = self.parse(roundtrip)
                assert_equal(set(imported_keys), set(recovered))
                assert_equal({pub: value[0] for pub, value in imported_keys.items()},
                             {pub: value[0] for pub, value in recovered.items()})
                self.record_check('payload_roundtrip_wifsets', version=version, mode=mode)
                for role, pubkey in self.roles.items():
                    address = key_to_p2pkh(pubkey)
                    assert_equal(wallet.dumpprivkey(address), recovered[pubkey][0])
                    fact = wallet.getaddressinfo(address)
                    assert_equal(fact['timestamp'], 1)
                    assert 'hdkeypath' not in fact
                    assert 'hdmasterfingerprint' not in fact
                for role, (raw, _) in self.spends.items():
                    signed = wallet.signrawtransactionwithwallet(raw)
                    assert_equal(signed['complete'], True)
                    assert_equal(node.testmempoolaccept([signed['hex']])[0]['allowed'], True)
                    self.record_check('imported_wallet_script_acceptances', version=version, mode=mode, role=role)
                self.record_check('payload_import_wallets', version=version, mode=mode)

    def check_watch_only(self, fixture):
        path = self.export(fixture, 'watch-only')
        payload, metadata, recovered = self.parse(path)
        assert_equal((payload, recovered), ([], {}))
        assert_equal(metadata[0]['counts']['private_keys'], 0)
        assert_equal(metadata[0]['counts']['roots'], 0)
        script = next(obj for obj in metadata if obj['type'] == 'script')
        assert_equal(script['kinds'], ['watch_metadata', 'watch_only'])
        for node, version in zip(self.nodes, [290500, 180100]):
            wallet = self.blank_wallet(node, version, f'watch-{version}')
            wallet.importwallet(str(path))
            address = script_to_p2sh(bytes.fromhex(script['script_hex']))
            assert_equal(wallet.getaddressinfo(address)['iswatchonly'], False)
            # Explicit test-only reconstruction of public comment metadata is
            # separate from Core's payload importer and cannot supply secrets.
            wallet.importaddress(script['script_hex'], '', False, True)
            assert_equal(wallet.getaddressinfo(address)['iswatchonly'], True)
            raw, _ = self.spends['multisig']
            assert_equal(wallet.signrawtransactionwithwallet(raw)['complete'], False)
            self.record_check('watch_only_no_authority', version=version)

    def run_test(self):
        self.check_counts = dict(independent_message_signatures=0, independent_script_acceptances=0,
            root_only_incomplete=0, payload_import_wallets=0, imported_wallet_script_acceptances=0,
            payload_roundtrip_wifsets=0, late_scan_controls=0, watch_only_no_authority=0)
        self.check_details = []
        self.root = Path(self.options.tmpdir) / 'recovery'
        self.root.mkdir(mode=0o700)
        self.inputs = self.root / 'inputs'
        self.inputs.mkdir(mode=0o700)
        self.output = self.root / 'output'
        self.output.mkdir(mode=0o700)
        self.empty_datadir = self.root / 'empty-datadir'
        self.empty_datadir.mkdir(mode=0o700)
        for node, version in zip(self.nodes, [290500, 180100]):
            assert_equal(node.getnetworkinfo()['version'], version)
            assert_equal(node.getconnectioncount(), 0)
        fixture = next(item for item in fixtures() if item.name == 'encrypted_hd')
        seed, receive, change, imported = fixture.keys
        script = b'\x52\x21' + imported.pubkey + b'\x21' + receive.pubkey + b'\x52\xae'
        records = tuple(record for record in fixture.records if record[0] != record_key('keymeta', ser_string(receive.pubkey)))
        root = ExtendedPrivateKey.from_seed(seed.secret)
        records += (key_metadata(receive, 12, path=b"m/0'/0'/0'", seed_id=seed.key_id,
            origin=(root._fingerprint(), [0x80000000, 0x80000000, 0x80000000])),
            (record_key('cscript', hash160(script)), ser_string(script)),
            pool_key(receive, 7, 15), pool_key(change, 8, 15, internal=True))
        fixture = replace(fixture, records=records, scripts=(script,))
        path = self.export(fixture, 'encrypted-mixed')
        recovered, self.root_only_wifs = self.check_independent_material(path, fixture)
        txid = self.funding_transactions(fixture, recovered)
        self.check_payload_import(path, recovered, txid)
        watch = next(item for item in fixtures() if item.name == 'watch_only')
        watch = replace(watch, records=((record_key('watchs', ser_string(script)), b'1'),
            (record_key('watchmeta', ser_string(script)), struct.pack('<iq', 1, 0))), scripts=(script,))
        self.check_watch_only(watch)
        assert_equal(list(self.empty_datadir.iterdir()), [])
        for node in self.nodes:
            assert_equal(node.getconnectioncount(), 0)
        assert_equal(self.check_counts, dict(independent_message_signatures=3, independent_script_acceptances=4,
            root_only_incomplete=2, payload_import_wallets=4, imported_wallet_script_acceptances=16,
            payload_roundtrip_wifsets=4, late_scan_controls=2, watch_only_no_authority=2))
        summary = dict(self.check_counts, check_details=self.check_details,
            unique_spend_templates=len(self.spends), early_transaction=txid,
            conservative_scan_blocks=self.nodes[0].getblockcount() + 1, late_control_scan_blocks=1,
            early_time=self.early_time, late_time=self.late_time,
            limitations='No general metadata importer, transaction-history backup or policy beyond tested legacy scripts')
        (self.root / 'recovery-results.json').write_text(json.dumps(summary, indent=2) + '\n')
        self.log.info('Recovery checks passed: 3 message signatures, 20 script validations on 4 spend templates, 2 root-only failures, 4 payload imports/roundtrips, 2 scan controls, 2 watch-only cases')


if __name__ == '__main__':
    ToolWalletLegacyExportRecoveryTest(__file__).main()
