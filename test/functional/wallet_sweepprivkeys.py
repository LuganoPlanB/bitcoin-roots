#!/usr/bin/env python3
"""Exercise transient-key sweep preview and broadcast without import."""
from decimal import Decimal
from pathlib import Path

from test_framework.address import key_to_p2pkh, key_to_p2sh_p2wpkh, key_to_p2wpkh
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet_util import generate_keypair

COIN = Decimal(100_000_000)

class SweepPrivKeysTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node, miner = self.nodes
        wallet = node.get_wallet_rpc(node.listwallets()[0])
        destination = wallet.getnewaddress()
        wif, pubkey = generate_keypair(wif=True)
        uncompressed_wif, uncompressed_pubkey = generate_keypair(compressed=False, wif=True)
        race_wif, race_pubkey = generate_keypair(wif=True)
        empty_wif, _ = generate_keypair(wif=True)
        self.generate(miner, 101)
        assert_raises_rpc_error(-6, 'No spendable UTXOs found', wallet.sweepprivkeys, [empty_wif], destination)
        for address in (key_to_p2pkh(pubkey), key_to_p2wpkh(pubkey), key_to_p2sh_p2wpkh(pubkey), key_to_p2pkh(uncompressed_pubkey)):
            miner.sendtoaddress(address, 1)
        race_address = key_to_p2pkh(race_pubkey)
        race_txid = miner.sendtoaddress(race_address, 1)
        self.generate(miner, 1)
        race_tx = miner.decoderawtransaction(miner.gettransaction(race_txid)['hex'])
        race_vout = next(vout for vout in race_tx['vout'] if vout['scriptPubKey'].get('address') == race_address)
        race_prevout = {'txid': race_txid, 'vout': race_vout['n'], 'scriptPubKey': race_vout['scriptPubKey']['hex'], 'amount': race_vout['value']}
        race_raw = miner.createrawtransaction([race_prevout], {miner.getnewaddress(): Decimal('0.99990000')})
        race_signed = miner.signrawtransactionwithkey(race_raw, [race_wif], [race_prevout])
        assert_equal(race_signed['complete'], True)
        miner.sendrawtransaction(race_signed['hex'])
        race_block = self.generate(miner, 1)[0]
        self.sync_blocks()
        assert_raises_rpc_error(-6, 'No spendable UTXOs found', wallet.sweepprivkeys, [race_wif], destination)
        node.invalidateblock(race_block)
        mempool_before_preview = wallet.getrawmempool()
        stale_preview = wallet.sweepprivkeys([race_wif], destination)
        assert_equal(stale_preview['broadcast'], False)
        assert_equal(stale_preview['inputs'], 1)
        assert_equal(wallet.getrawmempool(), mempool_before_preview)
        node.reconsiderblock(race_block)
        assert_raises_rpc_error(-6, 'No spendable UTXOs found', wallet.sweepprivkeys, [race_wif], destination)
        conflict_wif, conflict_pubkey = generate_keypair(wif=True)
        conflict_address = key_to_p2pkh(conflict_pubkey)
        conflict_txid = miner.sendtoaddress(conflict_address, 1)
        self.generate(miner, 1)
        self.sync_blocks()
        conflict_tx = miner.decoderawtransaction(miner.gettransaction(conflict_txid)['hex'])
        conflict_vout = next(v for v in conflict_tx['vout'] if v['scriptPubKey'].get('address') == conflict_address)
        prevout = {'txid': conflict_txid, 'vout': conflict_vout['n'], 'scriptPubKey': conflict_vout['scriptPubKey']['hex'], 'amount': conflict_vout['value']}
        raw = miner.createrawtransaction([prevout], {miner.getnewaddress(): Decimal('0.99990000')})
        competing = miner.signrawtransactionwithkey(raw, [conflict_wif], [prevout])
        conflict_id = miner.sendrawtransaction(competing['hex'])
        self.sync_mempools()
        rejected_state = wallet.listdescriptors(), wallet.listlabels(), wallet.getwalletinfo()['keypoolsize']
        assert_raises_rpc_error(-4, 'Sweep transaction broadcast failed: insufficient fee, rejecting replacement', wallet.sweepprivkeys, [conflict_wif], destination, True)
        assert_equal(wallet.getrawmempool(), [conflict_id])
        assert_equal((wallet.listdescriptors(), wallet.listlabels(), wallet.getwalletinfo()['keypoolsize']), rejected_state)
        assert_equal(wallet.getaddressinfo(conflict_address)['ismine'], False)
        descriptors = wallet.listdescriptors()
        labels = wallet.listlabels()
        keypoolsize = wallet.getwalletinfo()['keypoolsize']

        assert_raises_rpc_error(-5, 'Invalid private key', wallet.sweepprivkeys, ['invalid'], destination)
        assert_raises_rpc_error(-5, 'Invalid private key', wallet.sweepprivkeys, ['KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn'], destination)
        assert_raises_rpc_error(-8, 'Duplicate private key', wallet.sweepprivkeys, [wif, wif], destination)
        assert_raises_rpc_error(-4, 'Destination is not controlled', wallet.sweepprivkeys, [wif], miner.getnewaddress())
        node.createwallet(wallet_name='other')
        other = node.get_wallet_rpc('other')
        assert_raises_rpc_error(-4, 'Destination is not controlled', wallet.sweepprivkeys, [wif], other.getnewaddress())
        node.createwallet(wallet_name='watch', disable_private_keys=True)
        watch = node.get_wallet_rpc('watch')
        watch.importpubkey(wallet.getaddressinfo(destination)['pubkey'])
        assert_raises_rpc_error(-4, 'Destination is not controlled', watch.sweepprivkeys, [wif], destination)

        wallet.settxfee(Decimal('0.00001000'))
        preview = wallet.sweepprivkeys(privkeys=[wif, uncompressed_wif], destination=destination)
        assert_equal(preview['broadcast'], False)
        assert_equal(preview['inputs'], 4)
        decoded = node.decoderawtransaction(preview['hex'])
        required_fee = Decimal(decoded['vsize']) / COIN
        assert preview['fee'] >= required_fee
        assert preview['fee'] - required_fee <= Decimal('0.00000001')
        assert_equal(preview['amount'] - sum(output['value'] for output in decoded['vout']), preview['fee'])
        assert_equal(wallet.listdescriptors(), descriptors)
        assert_equal(wallet.listlabels(), labels)
        assert_equal(wallet.getwalletinfo()['keypoolsize'], keypoolsize)
        assert_equal(wallet.getaddressinfo(key_to_p2pkh(pubkey))['ismine'], False)
        assert_equal(wallet.getrawmempool(), [conflict_id])

        wallet.encryptwallet('pass')
        sent = wallet.sweepprivkeys([wif, uncompressed_wif], destination, True)
        assert_equal(sent['broadcast'], True)
        assert_equal(sent['txid'], preview['txid'])
        assert sent['txid'] in wallet.getrawmempool()
        rejected_descriptors = wallet.listdescriptors()
        rejected_labels = wallet.listlabels()
        rejected_keypoolsize = wallet.getwalletinfo()['keypoolsize']
        duplicate = wallet.sweepprivkeys([wif, uncompressed_wif], destination, True)
        assert_equal(duplicate['txid'], sent['txid'])
        assert_equal(set(wallet.getrawmempool()), {conflict_id, sent['txid']})
        assert_equal(wallet.listdescriptors(), rejected_descriptors)
        assert_equal(wallet.listlabels(), rejected_labels)
        assert_equal(wallet.getwalletinfo()['keypoolsize'], rejected_keypoolsize)
        assert_equal(wallet.getaddressinfo(key_to_p2pkh(pubkey))['ismine'], False)
        self.sync_mempools()
        self.generate(miner, 1)
        self.sync_blocks()
        assert_raises_rpc_error(-6, 'No spendable UTXOs found', wallet.sweepprivkeys, [wif, uncompressed_wif], destination)

        small_wif, small_pubkey = generate_keypair(wif=True)
        miner.sendtoaddress(key_to_p2pkh(small_pubkey), Decimal('0.00010000'))
        self.generate(miner, 1)
        self.sync_blocks()
        dust_destination = wallet.getnewaddress(address_type='legacy')
        wallet.settxfee(Decimal('0.00052000'))
        assert_raises_rpc_error(-4, 'Swept output would be dust', wallet.sweepprivkeys, [small_wif], dust_destination)
        wallet.settxfee(Decimal('0.10000000'))
        assert_raises_rpc_error(-6, 'Fee exceeds swept value', wallet.sweepprivkeys, [small_wif], destination)
        assert wif not in Path(self.options.tmpdir, 'node0', 'regtest', 'debug.log').read_text()

        # A sweep output is an ordinary wallet UTXO after confirmation. Exercise
        # it through explicit coin control only after the encrypted wallet has
        # restarted and been unloaded/reloaded, so neither lifecycle handling
        # nor a stale sweep state can silently substitute another input.
        lifecycle_wif, lifecycle_pubkey = generate_keypair(wif=True)
        miner.sendtoaddress(key_to_p2wpkh(lifecycle_pubkey), 1)
        self.generate(miner, 1)
        self.sync_blocks()
        lifecycle_preview = wallet.sweepprivkeys([lifecycle_wif], destination)
        lifecycle_sweep = wallet.sweepprivkeys([lifecycle_wif], destination, True)
        assert_equal(lifecycle_sweep['txid'], lifecycle_preview['txid'])
        self.sync_mempools()
        self.generate(miner, 1)
        self.sync_blocks()
        self.wait_until(lambda: any(utxo['txid'] == lifecycle_sweep['txid'] for utxo in wallet.listunspent()))
        lifecycle_utxo = next(utxo for utxo in wallet.listunspent() if utxo['txid'] == lifecycle_sweep['txid'])
        lifecycle_input = {'txid': lifecycle_utxo['txid'], 'vout': lifecycle_utxo['vout']}
        wallet.lockunspent(False, [lifecycle_input])
        assert_equal(wallet.listlockunspent(), [lifecycle_input])
        wallet.lockunspent(True, [lifecycle_input])

        wallet_name = wallet.getwalletinfo()['walletname']
        self.restart_node(0)
        wallet = self.nodes[0].get_wallet_rpc(wallet_name)
        assert_equal(wallet.getaddressinfo(key_to_p2pkh(pubkey))['ismine'], False)
        assert wif not in Path(self.options.tmpdir, 'node0', 'regtest', 'debug.log').read_text()
        self.nodes[0].unloadwallet(wallet_name)
        assert wallet_name not in self.nodes[0].listwallets()
        self.nodes[0].loadwallet(wallet_name)
        wallet = self.nodes[0].get_wallet_rpc(wallet_name)
        assert_equal(wallet.getaddressinfo(key_to_p2pkh(pubkey))['ismine'], False)
        wallet.walletpassphrase('pass', 60)
        controlled_send = wallet.send(
            outputs={miner.getnewaddress(): Decimal('0.5')},
            options={
                'inputs': [lifecycle_input],
                'add_inputs': False,
                'fee_rate': 1,
                'replaceable': True,
            },
        )
        assert_equal(controlled_send['complete'], True)
        controlled_tx = self.nodes[0].decoderawtransaction(self.nodes[0].getrawtransaction(controlled_send['txid']))
        assert_equal(len(controlled_tx['vin']), 1)
        assert_equal(controlled_tx['vin'][0]['txid'], lifecycle_input['txid'])
        assert_equal(controlled_tx['vin'][0]['vout'], lifecycle_input['vout'])
        assert controlled_tx['vin'][0]['sequence'] < 0xfffffffe
        assert controlled_send['txid'] in wallet.getrawmempool()
        assert lifecycle_wif not in Path(self.options.tmpdir, 'node0', 'regtest', 'debug.log').read_text()

if __name__ == '__main__': SweepPrivKeysTest(__file__).main()
