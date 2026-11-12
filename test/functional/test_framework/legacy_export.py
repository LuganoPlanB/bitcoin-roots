#!/usr/bin/env python3
# Copyright (c) 2009-2010 Satoshi Nakamoto
# Copyright (c) 2009-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Synthetic legacy records and clean BDB-v9 pages for offline export tests.

TEST KEYS ONLY. These are source-derived fixtures, not wallets emitted by old
executables. No exporter, node, Berkeley DB library or network is used.
Schemas: Core v0.18.1 fa27a0760792b251585f2a70eccdd547f915b7e4
wallet.h CWalletKey/CKeyPool and walletdb.h CHDChain/CKeyMetadata;
v0.13.0 a402396dce64c42ea73535b7dde4a9164d430438 and v0.15.0
3751912e8e044958d5ccea847a3f8eab0b026dc1 wallet.cpp seed/path semantics.
Modern payload oracle: v29.5 16ca532c8212d691daed14940da0f71f84f7b3fc
wallet/rpc/backup.cpp dumpwallet. Early encryption schema: v0.4.0
c7eb151ad0ed441d6fd598551059a9bbfb09e99e db.cpp/crypter.cpp/keystore.cpp.
Keep generator inputs and independent expected inventories separate from the
future exporter. Generated files belong in disposable test directories.
"""

from dataclasses import dataclass
import hashlib
import struct
import unittest

from test_framework.address import key_to_p2pkh
from test_framework.extendedkey import ExtendedPrivateKey
from test_framework.key import ECKey, ORDER
from test_framework.messages import hash256, ser_compact_size, ser_string
from test_framework.script import hash160
from test_framework.wallet_util import bytes_to_wif

DONORS = {
    'v0.4.0': 'c7eb151ad0ed441d6fd598551059a9bbfb09e99e',
    'v0.13.0': 'a402396dce64c42ea73535b7dde4a9164d430438',
    'v0.15.0': '3751912e8e044958d5ccea847a3f8eab0b026dc1',
    'v0.18.1': 'fa27a0760792b251585f2a70eccdd547f915b7e4',
    'v29.5': '16ca532c8212d691daed14940da0f71f84f7b3fc',
}


def record_key(family, suffix=b''):
    return ser_string(family.encode('ascii')) + suffix


def _der_tlv(tag, value):
    size = len(value)
    length = bytes([size]) if size < 128 else bytes([0x80 + (size.bit_length() + 7) // 8]) + size.to_bytes((size.bit_length() + 7) // 8, 'big')
    return bytes([tag]) + length + value


@dataclass(frozen=True)
class FixtureKey:
    secret: bytes
    compressed: bool = True

    @property
    def pubkey(self):
        key = ECKey()
        key.set(self.secret, self.compressed)
        return key.get_pubkey().get_bytes()

    @property
    def key_id(self):
        return hash160(self.pubkey)

    @property
    def wif(self):
        return bytes_to_wif(self.secret, self.compressed)

    @property
    def der(self):
        # Adapt ec_privkey_export_der (v0.18.1 key.cpp), also retained as
        # ec_seckey_export_der in current Roots. Explicit curve parameters
        # produce Bitcoin's historical 214/279-byte DER, not named-curve DER.
        def integer(value):
            return _der_tlv(2, value.to_bytes((value.bit_length() + 8) // 8, 'big'))
        generator = ECKey()
        generator.set((1).to_bytes(32, 'big'), self.compressed)
        field = _der_tlv(0x30, bytes.fromhex('06072a8648ce3d0101') + integer(2**256 - 2**32 - 977))
        curve = _der_tlv(0x30, bytes.fromhex('040100040107'))
        parameters = _der_tlv(0x30, integer(1) + field + curve + _der_tlv(4, generator.get_pubkey().get_bytes()) + integer(ORDER) + integer(1))
        payload = integer(1) + _der_tlv(4, self.secret) + _der_tlv(0xa0, parameters)
        payload += _der_tlv(0xa1, _der_tlv(3, b'\x00' + self.pubkey))
        return _der_tlv(0x30, payload)


def plain_key(key, checksum=True):
    value = ser_string(key.der)
    return record_key('key', ser_string(key.pubkey)), value + (hash256(key.pubkey + key.der) if checksum else b'')


def wallet_key(key, created=0, expires=0, comment=b'old\x00\xff comment'):
    # Adapted CWalletKey's disk serializer: stream version is a field here.
    value = struct.pack('<i', 180100) + ser_string(key.der)
    value += struct.pack('<qq', created, expires) + ser_string(comment)
    return record_key('wkey', ser_string(key.pubkey)), value


def key_metadata(key, version=1, created=0, path=b'', seed_id=b'\x00' * 20, origin=None):
    value = struct.pack('<iq', version, created)
    if version >= 10:
        value += ser_string(path) + seed_id
    if version >= 12:
        fingerprint, indexes = origin or (b'\x00' * 4, [])
        value += fingerprint + ser_compact_size(len(indexes))
        value += b''.join(struct.pack('<I', i) for i in indexes) + bytes([origin is not None])
    return record_key('keymeta', ser_string(key.pubkey)), value


def hd_chain(seed_id, split=False, external=1, internal=1):
    return record_key('hdchain'), struct.pack('<iI', 2 if split else 1, external) + seed_id + (struct.pack('<I', internal) if split else b'')


def pool_key(key, index, generation=13, internal=False, pre_split=False):
    stream_version = {13: 130000, 15: 150000, 18: 180100, 29: 259900}[generation]
    value = struct.pack('<iq', stream_version, 1231006505) + ser_string(key.pubkey)
    if generation >= 15:
        value += bytes([internal])
    if generation >= 18:
        value += bytes([pre_split])
    return record_key('pool', struct.pack('<q', index)), value


# Independent OpenSSL 3 AES-CBC answers for v0.4's source-specified algorithm.
# Master bytes 00..1f; salt 0000deadbeef0000; passphrase b'test'; 25000 rounds.
# KDF key/IV also match v29.5 wallet_crypto_tests::passphrase's fixed vector.
MASTER_CIPHERTEXT = bytes.fromhex('c840551c9d858c8222cfbeeba8c5c18c1abe8494a51c29a25ff90a9f54a0556bbb735241baa95adcecbca78ce08e4012')
ENCRYPTED_SECRETS = {
    'imported': '2441496926c27bd7dd108c7e3e6ae81d5802946847721d5cd2a91da0224f524493baca80841337640ba1cd7edcee0954',
    'uncompressed': 'f003da1309c1846ea200e8d10923f4736c28a7ba3c7b58a930a5aac6ba59ca0d64cba59ad23dc6b7c3b1418806757881',
    'seed': '83e8c9492c18bce70dc20b102e9556aa39e4a86cd38fff312093fd494ca62170bd11f2090a8ed678db06a5ea2b470b04',
    'external': '2db51dff441efe3b8d49abe4e1826f6bb80819fc0b14107015a59bc49690cc2867762d3663aa97baf45336dcd6f9d0ea',
    'internal': 'df759982053bba44c50d165731cd263f7b68cb972c3c3018b3ae3fdd5b4d3d088d0be6f09bb92d57cb4a16f7f39f02c5',
}


def master_key():
    value = ser_string(MASTER_CIPHERTEXT) + ser_string(bytes.fromhex('0000deadbeef0000'))
    return record_key('mkey', struct.pack('<I', 1)), value + struct.pack('<II', 0, 25000) + ser_string(b'')


def encrypted_key(key, vector, checksum=False):
    ciphertext = bytes.fromhex(ENCRYPTED_SECRETS[vector])
    return record_key('ckey', ser_string(key.pubkey)), ser_string(ciphertext) + (hash256(ciphertext) if checksum else b'')


@dataclass(frozen=True)
class LegacyFixture:
    name: str
    records: tuple
    keys: tuple = ()
    seeds: tuple = ()  # (FixtureKey, active); expected roots come from independent Python BIP32.
    scripts: tuple = ()
    donor: str = 'v29.5'
    passphrase: bytes | None = None


def fixtures():
    """Return reproducible families; no real release-produced wallet claim."""
    imported = FixtureKey((1).to_bytes(32, 'big'))
    old_key = FixtureKey((4).to_bytes(32, 'big'), False)
    seed = FixtureKey((2).to_bytes(32, 'big'))
    inactive = FixtureKey((3).to_bytes(32, 'big'))
    root = ExtendedPrivateKey.from_seed(seed.secret)
    external = FixtureKey(root.derive_path("m/0'/0'/0'").key.get_bytes())
    internal = FixtureKey(root.derive_path("m/0'/1'/0'").key.get_bytes())
    script = b'\x51' + bytes([len(imported.pubkey)]) + imported.pubkey + b'\x51\xae'
    common = [plain_key(imported), key_metadata(imported),
              (record_key('cscript', hash160(script)), ser_string(script)),
              (record_key('name', ser_string(key_to_p2pkh(imported.pubkey).encode())), ser_string(b'label \x00\xff%')),
              (record_key('purpose', ser_string(key_to_p2pkh(imported.pubkey).encode())), ser_string(b'receive'))]
    yield LegacyFixture('pre_hd', tuple([plain_key(old_key, False), wallet_key(imported), pool_key(old_key, 1)]), (old_key, imported), donor='v0.18.1')
    yield LegacyFixture('plain_mixed', tuple(common + [plain_key(old_key, False)]), (imported, old_key), scripts=(script,))
    for split in (False, True):
        generation = 15 if split else 13
        keys = (seed, external, internal) if split else (seed, external)
        records = [hd_chain(seed.key_id, split), plain_key(seed), key_metadata(seed, 10, path=b'm', seed_id=seed.key_id)]
        for index, (key, path) in enumerate([(external, b"m/0'/0'/0'"), (internal, b"m/0'/1'/0'")][:2 if split else 1]):
            records += [plain_key(key), key_metadata(key, 10, path=path, seed_id=seed.key_id), pool_key(key, index, generation, bool(index))]
        yield LegacyFixture('hd_split' if split else 'hd_unsplit', tuple(records), keys, ((seed, True),), donor='v0.15.0' if split else 'v0.13.0')
    records = common + [plain_key(seed), plain_key(inactive), hd_chain(seed.key_id, True),
                        key_metadata(seed, 12, path=b's', seed_id=seed.key_id),
                        key_metadata(inactive, 10, path=b's', seed_id=inactive.key_id),
                        plain_key(external), key_metadata(external, 12, path=b"m/0'/0'/0'", seed_id=seed.key_id,
                                                          origin=(root._fingerprint(), [0x80000000, 0x80000000, 0x80000000])),
                        pool_key(external, 7, 18, pre_split=True)]
    yield LegacyFixture('seed_rotated', tuple(records), (imported, seed, inactive, external), ((seed, True), (inactive, False)), (script,))
    uncompressed = FixtureKey(imported.secret, False)
    yield LegacyFixture('encrypted_non_hd', (master_key(), encrypted_key(uncompressed, 'uncompressed')), (uncompressed,), donor='v0.4.0', passphrase=b'test')
    records = [master_key(), hd_chain(seed.key_id, True), key_metadata(seed, 12, path=b's', seed_id=seed.key_id)]
    for key, vector in [(seed, 'seed'), (external, 'external'), (internal, 'internal'), (imported, 'imported')]:
        records.append(encrypted_key(key, vector, checksum=True))
    records += [key_metadata(external, 10, path=b"m/0'/0'/0'", seed_id=seed.key_id),
                key_metadata(internal, 10, path=b"m/0'/1'/0'", seed_id=seed.key_id)]
    yield LegacyFixture('encrypted_hd', tuple(records), (seed, external, internal, imported), ((seed, True),), passphrase=b'test')
    yield LegacyFixture('watch_only', ((record_key('watchs', ser_string(script)), b'1'),
                                      (record_key('watchmeta', ser_string(script)), struct.pack('<iq', 1, 0))), scripts=(script,))
    yield LegacyFixture('empty', ())
    yield LegacyFixture('mkey_only', (master_key(),))


def bdb_bytes(records, page_size=4096, byteorder='little'):
    """Synthetic B-tree v9 SUBDB/main, with leaf/internal/overflow pages.

    Layout follows Roots migrate.cpp MetaPage/PageHeader/RecordHeader; all LSNs
    are clean (0,1). Record encoding always remains Bitcoin little endian;
    byteorder applies only to the BDB envelope. Not a general BDB writer.
    """
    if page_size not in (512, 1024, 2048, 4096, 8192, 16384, 32768, 65536):
        raise ValueError('unsupported BDB page size')
    if byteorder not in ('little', 'big'):
        raise ValueError('unsupported BDB byte order')
    endian = '<' if byteorder == 'little' else '>'
    pages = [None] * 4

    def allocate():
        pages.append(None)
        return len(pages) - 1

    def header(number, kind, count, free, level=1, previous=0, following=0):
        return struct.pack(endian + '5I2H2B', 0, 1, number, previous, following, count, free, level, kind)

    def data_page(number, items, kind=5, level=1):
        page = bytearray(page_size)
        offset = page_size
        indexes = []
        for item in items:
            offset -= len(item)
            indexes.append(offset)
            page[offset:offset + len(item)] = item
        if 26 + 2 * len(items) > offset:
            raise ValueError('fixture exceeds single-level tree capacity')
        page[:26] = header(number, kind, len(items), offset & 0xffff, level)
        for index, offset in enumerate(indexes):
            struct.pack_into(endian + 'H', page, 26 + 2 * index, offset)
        return bytes(page)

    def item(value):
        if len(value) <= page_size // 4:
            return struct.pack(endian + 'HB', len(value), 1) + value
        chunks = [value[i:i + page_size - 26] for i in range(0, len(value), page_size - 26)]
        numbers = [allocate() for _ in chunks]
        for i, (number, chunk) in enumerate(zip(numbers, chunks)):
            page = header(number, 7, 1, len(chunk), 0, numbers[i - 1] if i else 0,
                          numbers[i + 1] if i + 1 < len(numbers) else 0) + chunk
            pages[number] = page.ljust(page_size, b'\x00')
        return struct.pack(endian + 'HBBII', 0, 3, 0, numbers[0], len(value))

    records = sorted(records)
    if len({key for key, _ in records}) != len(records):
        raise ValueError('duplicate fixture record')
    leaves = []
    pending = []
    first = b''
    used = 26
    for key, value in records:
        pair = [item(key), item(value)]
        size = sum(map(len, pair)) + 4
        if pending and used + size > page_size:
            number = allocate()
            pages[number] = data_page(number, pending)
            leaves.append((first, number))
            pending, used = [], 26
        if not pending:
            first = key
        pending.extend(pair)
        used += size
    if not leaves:
        pages[3] = data_page(3, pending)
    else:
        number = allocate()
        pages[number] = data_page(number, pending)
        leaves.append((first, number))
        internal = [struct.pack(endian + 'HBBII', len(key) if i else 0, 1, 0, number, 0) + (key if i else b'')
                    for i, (key, number) in enumerate(leaves)]
        pages[3] = data_page(3, internal, 3, 2)

    # IsBDBFile requires a 4 KiB minimum even when individual pages are smaller.
    while len(pages) * page_size < 4096:
        number = allocate()
        pages[number] = header(number, 0, 0, 0, 0).ljust(page_size, b'\x00')

    def meta(number, root):
        page = bytearray(page_size)
        struct.pack_into(endian + '6I4B6I20s5I', page, 0, 0, 1, number, 0x053162, 9, page_size,
                         0, 9, 0, 0, 0, len(pages) - 1, 0, 0, 0, 0x20, b'\x00' * 20, 0, 2, 0, 0, root)
        return bytes(page)

    pages[0] = meta(0, 1)
    pages[1] = data_page(1, [struct.pack(endian + 'HB', 4, 1) + b'main', struct.pack(endian + 'HB', 4, 1) + struct.pack('>I', 2)])
    pages[2] = meta(2, 3)
    return b''.join(pages)


class TestFrameworkLegacyExport(unittest.TestCase):
    def test_known_keys_and_bip32(self):
        key = FixtureKey((1).to_bytes(32, 'big'))
        self.assertEqual(key.pubkey.hex(), '0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798')
        self.assertEqual(len(key.der), 214)
        self.assertEqual(len(FixtureKey(key.secret, False).der), 279)
        # Published BIP32 test vector 1, also pinned in Core's bip32_tests.cpp.
        root = ExtendedPrivateKey.from_seed(bytes(range(16)))
        self.assertEqual(root.to_string(mainnet=True), 'xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi')

    def test_fixture_inventory(self):
        inventory = {fixture.name: fixture for fixture in fixtures()}
        self.assertEqual(len(inventory), 10)
        self.assertEqual(len(inventory['seed_rotated'].seeds), 2)
        self.assertEqual(len(inventory['encrypted_hd'].keys), 4)
        self.assertFalse(inventory['watch_only'].keys)
        self.assertFalse(inventory['empty'].records)
        for fixture in inventory.values():
            self.assertEqual(len(dict(fixture.records)), len(fixture.records))
            self.assertIn(fixture.donor, DONORS)

    def test_storage_determinism_and_bounds(self):
        records = next(f for f in fixtures() if f.name == 'seed_rotated').records
        for size in (512, 4096, 65536):
            for order in ('little', 'big'):
                with self.subTest(size=size, byteorder=order):
                    data = bdb_bytes(records, size, order)
                    self.assertEqual(data, bdb_bytes(reversed(records), size, order))
                    self.assertEqual(len(data) % size, 0)
        with self.assertRaises(ValueError):
            bdb_bytes(records, 513)
        with self.assertRaises(ValueError):
            bdb_bytes([(b'key', b'a'), (b'key', b'b')])

    def test_early_kdf_vector(self):
        digest = hashlib.sha512(b'test' + bytes.fromhex('0000deadbeef0000')).digest()
        for _ in range(24999):
            digest = hashlib.sha512(digest).digest()
        self.assertEqual(digest[:32].hex(), 'fc7aba077ad5f4c3a0988d8daa4810d0d4a0e3bcb53af662998898f33df0556a')
        self.assertEqual(digest[32:48].hex(), 'cf2f2691526dd1aa220896fb8bf7c369')
