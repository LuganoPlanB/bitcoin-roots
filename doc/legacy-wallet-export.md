# Offline legacy wallet export: proposed version 1 contract

**Implementation status:** this document specifies planned `bitcoin-wallet
dumpwallet` behavior. The command is not implemented at this baseline. The
deterministic fixtures and source comparisons described below are available;
they do not establish historical whole-wallet compatibility or a released
`v32.1-roots.2`. Operator instructions will be checked against the implemented
command before release.

The proposed command exports decoded private keys, positively identified HD
seeds and BIP32 roots, scripts and recovery metadata from a stable offline
legacy BDB copy. Keep the original wallet backup. The export contains
unencrypted spending material when private keys are present; its metadata is
private information too. It does not preserve transaction history, a balance,
chain state, a BIP39 mnemonic, or every importer's wallet state. An HD root does
not cover unrelated imported keys. Every recoverable individual key is exported
even when roots are available. A non-HD wallet has no root.

## Proposed invocation and input

```sh
# Proposed; use a clean offline copy and an existing empty datadir.
bitcoin-wallet -datadir=/secure/empty-datadir -chain=main \
  -wallet=/secure/offline-copy/wallet.dat \
  -dumpfile=/secure/export/wallet.keys dumpwallet

# A trusted parent supplies the passphrase through stdin, never in this command.
bitcoin-wallet -datadir=/secure/empty-datadir -chain=main \
  -wallet=/secure/offline-copy/wallet.dat \
  -dumpfile=/secure/export/wallet.keys -stdinwalletpassphrase dumpwallet
```

`-wallet`, `-dumpfile` and `-chain` must each occur explicitly once with a
nonempty `=value`. `-chain` is one of `main`, `test`, `testnet4`, `signet`,
`regtest`; aliases such as `-regtest` alone do not satisfy this requirement and
are rejected for this command. It selects encodings, not an intrinsic property
authenticated by wallet.dat. Main uses ordinary mainnet WIF/xprv prefixes; the
other chains use their native test-family prefixes. Do not silently retry a
different chain. `-datadir` retains the tool's existing behavior; an explicit
disposable datadir is recommended, including for help. No config file or
environment value can supply a wallet passphrase.

The only command-specific options are `-wallet`, `-dumpfile` and the bare
boolean switch `-stdinwalletpassphrase`. Value-bearing or negated forms of
that switch, duplicate options, extra positional arguments and unknown options
are errors before wallet access, prompting or output creation. Existing generic
help/version, datadir, debug and printtoconsole options retain their ordinary
meaning; debug must never log arguments, passphrase bytes or decoded secrets.
Reject other chain-customization switches for this command. Errors for rejected
arguments report the option/category without echoing supplied values. No
passphrase-value, file, environment or config option is added. Existing `dump`,
`createfromdump`, `info`, `create`, RPC and GUI contracts remain separate.

The source is an existing regular file or a directory containing `wallet.dat`.
An absolute file path is supported by the new command; it must bypass the raw
tool's directory-oriented wallet-loading path. Relative paths resolve against
the current working directory. Reject a symlink at the source file's final
component, special files, missing input, SQLite and non-BDB formats. The source
must be a stable copy made after clean shutdown. No node, chainstate, blocks,
RPC, synchronization, rescan, migration, CWallet load/unlock, database recovery,
upgrade, address generation, seed rotation or source-side lock/log is permitted.

Accept the current reader's version-9 B-tree layout, SUBDB/main, no built-in BDB
encryption, clean LSNs `(0,1)`, both byte orders and page sizes
512/1024/2048/4096/8192/16384/32768/65536. Require at least 4096 file bytes, as
the current BDB detector does. Preserve migrate.cpp's documented allowances
for a trailing partial page and stale inner `last_page`; referenced data must
still be completely present and bounded. Do not add a file-size-mod-page-size
rule. Repeated pages within an overflow chain/cycles are fatal; shared overflow
pages across distinct records require consistent lengths and bounded total
visits. Reject duplicate physical keys before map insertion can hide them.

Compare source handle identity/size/mtime before reading and after inventory
validation, and verify that the named source still identifies that file before
publication. Detected changes fail. These observations cannot prove a snapshot
against an adversarial concurrent writer. Reads may update atime; forensic
metadata preservation requires an externally read-only mount.

### Passphrase bytes

Prompt on the controlling terminal with echo disabled only when ckey records
need decryption. Restore the saved terminal state on handled exits. Plaintext,
watch-only, empty and mkey-only inventories require no prompt; reject the stdin
switch for them. An mkey-only inventory cannot verify a password against a
private/public pair and must never claim recovered authority.

Stdin mode reads one bounded byte string through LF, CRLF or EOF. Strip only
that terminator. Preserve spaces, UTF-8 bytes without normalization and embedded
NUL; an empty terminated line is an empty passphrase, while EOF before any byte
is missing input. Reject trailing bytes already buffered/readily available in
the same pipe after the terminator. Do not wait for another byte after a complete
line from a live pipe. Input arriving later is neither consumed nor claimed to
have been validated. Terminal collection is length-aware with the same trimming
and byte limit; embedded NUL must not truncate input. POSIX terminal bytes pass
through; native Windows console Unicode is encoded as UTF-8 without normalization,
rejecting invalid surrogate input. Explicit stdin remains a raw byte path. The
same representable input must reach crypto as identical bytes in either mode.
Do not retry altered bytes, old NUL-truncated variants, or another passphrase.

## Record dispositions and validation

Every live BDB key/value record is accounted for once, before semantic
deduplication. Deleted pairs, unreachable/free pages and page slack are not live
wallet records and are not forensically recovered. Deletion markers that would
mis-pair live keys and values are fatal. Required data errors are fatal,
with no partial-success mode. For equal public-key identities, equal private
material may be merged while all metadata associations are retained; conflicting
material is fatal. Compressed and uncompressed public-key encodings are distinct
identities and retain separate WIFs. Missing metadata means unclassified,
not evidence that a key was imported or used for change.

| Record family | Version 1 disposition |
| --- | --- |
| `key` | Decode current-supported DER, optional absent or exactly 32-byte pubkey/DER checksum. Verify checksum when present and private/public agreement regardless of checksum. Emit individual WIF and key metadata. Do not impose a new ASN.1 canonicality rule beyond current CKey::Load and the bounds below. |
| `wkey` | Adapt v0.18.1 CWalletKey: int32 stream version, DER vector, int64 created/expiry, comment; optional absent or 32-byte checksum outside the wrapper. Export the key and all wrapper metadata. |
| `ckey` | Public key plus ciphertext vector; optional absent or 32-byte ciphertext checksum. Require correct checksum, decryption, 32-byte scalar and public-key agreement. Emit WIF after full validation. |
| `mkey` | uint32 ID and CMasterKey schema. Accept method0, salt8, 48-byte encrypted master, empty other-parameters vector and bounded nonzero iterations. Retain public parameter metadata; never export the decrypted symmetric master or ciphertext. |
| `hdchain` | One exact v1 or v2 record: version, external counter, seed ID, and internal counter only in v2. Preserve stored counters; do not reconstruct or increment them. |
| `keymeta` | Exact metadata versions 1, 10 or 12. v1 has version/time; v10 adds byte path/seed ID; v12 adds four-byte origin fingerprint, uint32 path vector and has-origin bool. Preserve raw fields and unknown times. Other versions fail rather than inferring their production from donor `>=` conditions. |
| `pool` | int64 index and stream-version/time/pubkey, followed by zero, one or two bool bytes (internal, pre-split). Preserve field presence; absent bools default false. Each present bool must be 0 or 1. No keypool refill/consumption. |
| `cscript` | Require script hash160 key to match the serialized script; validate push-data boundaries. Export redeem-script payload and metadata. Do not require the script to be standard or independently spendable. |
| `watchs` | Script key and exactly ASCII `0` or `1`; preserve disabled/enabled state. Emit watch metadata, not a redeem-script payload merely because a watch script exists. |
| `watchmeta` | Script key plus the same supported metadata schema; preserve even without a matching watchs/cscript record. Validate script push-data boundaries. |
| `name`, `purpose` | Preserve address and value bytes, including empty/control/non-UTF8 values, in typed address metadata. Only valid destinations matching an exported public key under the selected chain contribute legacy-line labels. Other addresses/labels remain present without an invented public-key association. |
| `defaultkey` | One fully validated public key; preserve identity as public metadata. Do not assume its private key exists. |
| `version` | One exact nonnegative int32 writer version, informational; retain its original value, including historical 10300. It is not a release-compatibility assertion. |
| `minversion` | One exact nonnegative int32, at most 169900 (v29.5's last legacy feature level). A higher minimum requirement fails. Absence is null, not an inferred version. |
| `flags` | One uint64. Retain known and unknown tolerable lower32 bits. Accept upper disable-private-keys and blank flags; reject descriptor/external-signer or unknown upper bits. A disable-private-keys flag with key/wkey/ckey is inconsistent and fatal. Blank is informational: legacy imports need not clear it. |

An optional checksum must have exactly the documented size; truncated checksums
and arbitrary trailing record bytes fail. All required public keys must be valid
33/65-byte curve points. For the non-omitted schemas above, keys and values must
be consumed completely. Labels/path bytes may be unrecognized without becoming
an unknown record family; they are retained losslessly.

Known lower flag bits are 0 (avoid_reuse), 1 (key_origin_metadata), and 2
(last_hardened_xpub_cached). Known upper bits are 32 (disable_private_keys),
33 (blank), 34 (descriptors, rejected), and 35 (external_signer, rejected).
`unknown_tolerable_flags` is `flags & 0x00000000fffffff8`, or zero when flags
is absent; no unknown upper bit is tolerable.

Try mkey slots in ascending unsigned ID order, charging the KDF budget before
each attempt, and validate unresolved ckeys in public-key byte order. Stop when
all required ckeys have been proved against their public keys. Unused slots are
not password-verified. Exhausting the work budget is a limit error, not a claim
that the passphrase is wrong.

The following known non-secret families are counted and omitted, with a separate
`omitted` metadata entry for each family present: `tx`, `wtxvariant`, `acentry`,
`acc`, `bestblock`, `bestblock_nomerkle`, `orderposnext`, `destdata`, `lockedutxo`,
historical `setting`, and current `settings`. Omitted keys/values are opaque
bounded bytes; successful export does not validate transaction/accounting data
or restore settings, receive requests, spent/locked state, ordering or history.
No omitted raw value is placed in the output.

Descriptor families `walletdescriptor`, `walletdescriptorcache`,
`walletdescriptorlhcache`, `walletdescriptorkey`, `walletdescriptorckey`,
`activeexternalspk`, `activeinternalspk` are fatal even inside BDB. Foreign
database records such as `addr`, `blockindex`, `owner`, every other unlisted
family, an undecodable type name and incompatible flags are fatal. This is an
exhaustive allowlist, not a default unknown-record skip.

### Seed evidence and derivation

Seed identities come only from a nonzero hdchain seed ID, a keymeta `m`/`s` marker referring to its
own key ID, or an explicit nonzero hd_seed_id reference. Never identify a seed
from a fingerprint match, ordinary private key, label or symmetric mkey. A
contradictory self marker is fatal. The hdchain ID alone determines active
status; an all-zero hdchain ID means unset and identifies no seed. All other
identified seeds are inactive. Derive each recoverable root
independently from that seed's original 32 bytes. Require a valid BIP32 root and
export both its xprv/tprv and the seed's ordinary WIF payload line.

For inventories containing private material, every nonzero HD seed reference
must resolve to a present, validated private seed; missing required material
fails. In genuinely secret-free inventories retain unresolved seed IDs/public
metadata with null roots, zero recovered-root count and no authority claim.
No HD record or seed evidence means no seed/root entry.

Validate recognized Core child paths `m/0'/0'/i'` and `m/0'/1'/i'`, with
`0 <= i < 2^31`, using checked derivation and public-key comparison. v1 chains
have only the external path; a claimed internal path for the active v1 chain
is inconsistent. For v12 metadata, a present origin path is authoritative;
where text expresses a parseable BIP32 path it must agree with the active origin
vector, even when the path is outside the supported Core template. A nonzero
local seed reference with an active origin must match that root public key's
four-byte fingerprint. A recognized path with a mismatched public key is fatal. Unsupported or
non-UTF8 paths remain exact bytes, with `derivation="unrecognized"` unless a
supported origin independently verifies the child. Imported origins without
an hd_seed_id are retained without inventing a local seed. An unrelated key's
WIF is never omitted on the assumption that some root covers it.

## Text file and deterministic order

The complete file is UTF-8 with LF endings and a final LF. Fixed header:

```text
# Bitcoin Roots offline legacy key export
# roots-dumpwallet-version: 1
# roots-network: <selected chain>
```

No fabricated best block, height, balance or birth-time inference appears.
There is no volatile creation timestamp; the same validated inventory and
selected chain produce the same bytes. First emit the wallet metadata object,
then the active-root compatibility comment if a root was recovered:
`# extended private masterkey: <encoded root>`. Emit key payloads, redeem-script
payloads, then remaining metadata grouped by type in this order: hdchain, key,
seed, pool, script, address, encryption, omitted. End with these two lines:

```text
# End of dump
# roots-export-complete: 1
```

Key payload grammar is `WIF SP TIME SP TOKEN SP # SP addr=ADDRESSES` with an
optional ` SP hdkeypath=PATH` comment for a recognized, verified origin/path.
TIME is UTC `YYYY-MM-DDTHH:MM:SSZ`. TOKEN priority is known matching label
`label=ESCAPED`, active seed `hdseed=1`, reserve `reserve=1`, inactive seed
`inactivehdseed=1`, verified internal child `change=1`, otherwise
`unclassified=1`. The last token is deliberately neutral: v29.5 importwallet
ignores it and uses its ordinary empty-label default. Metadata distinguishes
unknown classification from a known empty label. All applicable roles remain
in metadata even when a higher-priority legacy token masks them.

ADDRESSES are selected-chain encodings of matching known destinations, sorted
by their ASCII bytes and joined with commas; without an address-book match use
the P2PKH destination of the exact public key. For multiple matching labels,
choose the label attached to the lexically first matching address for the legacy
line and retain every label/address separately in metadata. ESCAPED adapts
v29.5 EncodeDumpString: bytes <=32, >=128 or `%` become `%` plus two lowercase
hex digits; all other bytes pass through. This is byte escaping, not Unicode
normalization. Never put a raw label/path into a line or diagnostic.

The effective time is the earliest positive representable stored keymeta/wkey
creation time (at most 253402300799), otherwise unknown. Preserve every raw
signed timestamp separately. Pool times are not substituted for missing key
birth times. Unknown uses `1970-01-01T00:00:01Z` in payloads, and null in
metadata; never use now. Redeem-script lines are `HEX SP TIME SP script=1 SP # SP
addr=P2SH_ADDRESS`, using script metadata time or the same epoch fallback.
Watch-only scripts without cscript appear only in typed metadata. The one-second
epoch fallback is deliberate: v29.5 importwallet ignores a zero script timestamp
when choosing its scan start, whereas one requests a scan before the first
Bitcoin block. This differs from the donor's unknown-script `0` output and is
not a claim that the key/script was created at that instant.

Sort key lines by effective time (unknown treated as zero), then raw public-key
bytes. Sort redeem scripts by raw script ID bytes. Sort metadata identities as
follows: key by pubkey; seed/script by ID; pool by signed index; address by raw
address bytes; encryption by unsigned ID; omitted by ASCII family. One hdchain
object is permitted. All nested identity arrays are unique and byte-sorted,
except origin paths and timestamp sources, which retain their defined order.

## roots-meta-v1 schema

Each metadata line is `# roots-meta-v1 ` followed by one compact JSON object
serialized by the existing UniValue library. No hand-built JSON escaping, raw
unknown database values, raw DER, ciphertext or decrypted mkey is emitted.
Control characters are JSON escapes and cannot introduce another physical line.
Object members are emitted in table order for determinism; readers use names,
not member order. All listed members are required; absent data uses null or an
empty array, never an omitted member. No additional record types/fields belong
to version1. Unknown future envelopes require a versioned consumer decision.

Common types:

- **Bytes**: `{"encoding":"utf8","value":STRING}` for exact valid UTF-8,
  otherwise `{"encoding":"hex","value":LOWERCASE_HEX}`. No normalization.
  Addresses, labels, purposes, original paths and wkey comments use this type.
- **ID**: lowercase hex of the raw serialized 20-byte Hash160 digest, not a
  reversed numeric display. **Pub**: lowercase hex of the original 33/65 bytes.
  Fingerprints are the four original bytes in lowercase hex.
- **Time64/Index64**: canonical signed decimal JSON string, preserving int64
  without a consumer's floating-point rounding. Effective times are Time64 or
  null; raw zero/negative/out-of-calendar timestamps remain exact Time64.
  uint32/int32/counts are JSON integers; uint64 flags are 16-digit hexadecimal
  strings in numeric most-significant-digit-first order.
- **Metadata**: object members `version` (1/10/12), `created` (raw Time64),
  `hd_path` (Bytes or null before v10), `hd_seed_id` (ID or null before v10/zero
  ID), `origin` (null before v12, otherwise object `has_origin` bool,
  `fingerprint` hex8, `path` array<uint32>). Preserve unused origin fields when
  has_origin is false; they do not establish a derivation.

| type | Required members after `type`, in order |
| --- | --- |
| `wallet` | `network` (chain string), `format` (`"bdb-v9"`), `page_size` (integer), `byte_order` (`"little"`/`"big"`), `source_bytes` (integer), `file_version` (int32/null), `min_version` (int32/null), `flags` (hex16/null), `unknown_tolerable_flags` (hex16), `default_pubkey` (Pub/null), `state` (below), `counts` (below) |
| `hdchain` | `version` (1/2), `seed_id` (ID), `external_counter` (uint32), `internal_counter` (uint32/null in v1) |
| `key` | `pubkey` (Pub), `key_id` (ID), `has_private_key` (bool), `source_families` (ASCII-sorted subset of key/wkey/ckey/keymeta/pool/defaultkey), `encrypted_in_source` (bool), `classification` (`"seed"`, `"hd_receive"`, `"hd_change"`, `"unclassified"`), `reserve` (bool), `created_at` (effective Time64/null), `metadata` (Metadata/null), `wkey` (null or object `stream_version` int32, `created` Time64, `expires` Time64, `comment` Bytes), `derivation` (`"not_applicable"`, `"seed"`, `"verified"`, `"unrecognized"`) |
| `seed` | `seed_id` (ID), `pubkey` (Pub/null), `active` (bool), `has_secret` (bool), `xprv` (selected-chain string/null), `evidence` (ASCII-sorted subset of `"active_hdchain"`, `"metadata_reference"`, `"seed_marker"`). has_secret is true exactly when xprv is non-null; its seed WIF is the corresponding key payload, not a symmetric master. |
| `pool` | `index` (Index64), `stream_version` (int32), `pubkey` (Pub), `time` (raw Time64), `internal` (bool), `internal_present` (bool), `pre_split` (bool), `pre_split_present` (bool) |
| `script` | `script_id` (ID), `script_hex` (lowercase hex), `kinds` (ASCII-sorted subset of `"redeem"`, `"watch_metadata"`, `"watch_only"`), `watch_enabled` (bool/null when watchs absent), `created_at` (effective Time64/null), `metadata` (Metadata/null). No signing-authority inference. |
| `address` | `address` (Bytes), `label` (Bytes/null), `purpose` (Bytes/null), `pubkeys` (byte-sorted matching Pub array, empty when no selected-chain match) |
| `encryption` | `id` (uint32), `method` (0), `iterations` (uint32), `salt` (hex16), `verified_with_private_key` (bool), `used_for_pubkeys` (byte-sorted Pub array). Decrypting padding alone never sets verified_with_private_key. Unused/mkey-only slots remain false/empty. |
| `omitted` | `family` (one allowlisted ASCII name), `records` (positive count) |

One key object exists for each explicit public-key identity from key/wkey/ckey,
keymeta, pool or defaultkey, including public-only identities. Script contents
do not invent separate key identities. Merge name and purpose only for identical
raw address bytes. Private material for an equal public-key identity must agree;
so must records describing an equal metadata identity (same pubkey and family,
pool index, raw address and family, or script ID and metadata family). Conflicts
fail rather than selecting a winner. Independent associations, such as pool
flags and key paths, remain separate facts. Original source-family
associations and wkey fields survive deduplication. Independently stored
keymeta/wkey creation times may differ: preserve both and use the conservative
earliest usable value rather than treating that difference as a conflict.
Seed evidence `metadata_reference` means an explicit nonzero hd_seed_id in key
metadata, including a self-reference; it does not claim a verified child path.

Classification is seed for an identified seed identity, otherwise hd_receive
or hd_change only for a verified local Core child on the corresponding branch;
all other identities are unclassified. Reserve membership is independent.
Derivation is seed for seed identities, verified for validated local children,
unrecognized for remaining nonempty paths, active origins or nonzero seed
references, and not_applicable when none of those provenance fields exists.

The wallet `counts` object has these integer members in order: `records`,
`private_keys`, `public_key_identities`, `identified_seeds`, `roots`, `scripts`,
`addresses`, `reserve_entries`, `encryption_keys`, `omitted_records`,
`unverified_derivations`. They count live input records before semantic merging, unique WIF-bearing
public-key identities, all explicit public-key identities, all positively
referenced seed IDs, recovered roots, unique scripts, raw address identities,
pool entries, mkey slots, omitted live records and keys with unrecognized
derivation respectively. One key used in several roles is counted once.

`state` is `"private"` if any private keys exist; otherwise `"watch_only"` if
an enabled watchs exists; otherwise `"metadata_only"` if public keys, scripts,
addresses, seed IDs or mkey slots remain; otherwise `"empty"`. Empty and
public-only output explicitly have zero private-key/root counts. A recovered
private key does not assert that every associated script is spendable.

## Fixed version 1 resource limits

These are explicit support limits, not statements about every historical wallet.
Reject excess before the relevant allocation/work, report unsupported limits
without exposing data, and publish nothing. No truncation, skipped required
record, reduced KDF count or automatic override option is permitted.

| Resource | Inclusive limit |
| --- | --- |
| Source file | 512 MiB; lower bound 4096 bytes |
| Live records before semantic deduplication | 1,000,000; private-key public identities 100,000 |
| Encoded database key | 1 MiB; key plus value for one pair 16 MiB |
| Aggregate decoded key/value bytes | 256 MiB, including allowlisted omissions |
| Serialized script bytes | 8 MiB; DER private-key vector 4096 bytes |
| Each address/label/purpose/path/wkey-comment byte string | 65,536 bytes |
| Origin path | 255 uint32 elements; supported Core child verification uses exactly three |
| Master-key slots | 32 |
| KDF | method0 only; 1 through 10,000,000 iterations per slot; at most 20,000,000 summed across attempted slots per invocation |
| Passphrase | 4096 bytes, excluding the single permitted terminator |
| Logical page visits | 2,000,000 total; each referenced page/offset/overflow declared length must also fit the file and decoded-byte budgets |
| Derived child steps | 1,000,000 total; checked failure never silently chooses another child |
| Metadata JSON line | 32 MiB, including prefix and LF |
| Complete output | 1 GiB, including headers/metadata/line endings |

Bounds come from a declared resource envelope checked against the synthetic
corpus, not an inferred maximum for Core releases. The ten fast fixtures contain
at most 13 records, 71-byte database keys, 282-byte values, 16 KiB ordinary
4096-page files, 25,000 KDF iterations and four passphrase bytes. Additional
storage cases exercise a 12,000-byte script with chained 512-byte overflow pages
and every supported page size/byte order. Limits deliberately leave substantial
headroom while keeping aggregate bytes, counts and cryptographic work finite.
The 10-million-round cap is also exercised by a native synthetic KDF probe;
host timings are evidence for selecting a budget, not a timeout or portability
promise. An otherwise valid wallet above a limit is unsupported by version1,
not evidence of a wrong passphrase or corrupt wallet. Future changes to these
limits require renewed boundary/resource tests and explicit documentation.

## Publication, status and recovery limits

The destination parent directory must already exist. Reject `-`, existing
regular files, directories, symlinks including dangling ones, devices and source
aliases. Resolve/open the destination parent once and anchor subsequent work to
that directory identity. Create an unpredictable same-directory temporary file
exclusively with private permissions (POSIX0600 or an owner-restricted native
ACL), checking the result immediately. Never create output beside the source
as a side effect of opening it.

Validate/decrypt the full required inventory before final publication. Check all
write, flush, file-sync and close results before an atomic native no-replace
move to the final name. Publication is the final fallible commit step; a
platform/filesystem without a usable no-replace primitive must fail closed.
Do not use exists()+ofstream, overwriting rename, or a fallback that introduces
a post-publication failure requiring unsafe deletion of the final name.
Pre-publication errors remove only invocation-owned temporary output and leave
no final export. Never unlink an existing user file. Do not promise directory
power-loss durability or secure erasure. A crash can leave a private temporary
file containing secrets; retain the original backup independently.

Exit0 means the complete file was published. Success stdout contains only the
destination, state and non-secret counts; warn about unencrypted spending
material when private_keys is positive. Other outcomes use the tool's ordinary
nonzero failure path. Diagnostics identify categories/record counts, not
passphrases, private keys, roots, raw records, labels or arbitrary argument
values. Missing/corrupt required data, wrong passphrase, unsupported formats,
unknown families/flags, limits, detected source changes, aliases, races and
write/permission failures cannot be reported as partial recovery success.

Old Core importwallet ignores comments, including all roots-meta-v1 objects and
root comments. Its WIF/script payload import does not restore exact labels on
every destination, original paths/origins, inactive/active root relationships,
watch-only metadata, keypool state, expiry, settings or history. The neutral
token also does not restore the original key classification. Unknown-time
payloads deliberately request the earliest conservative scan bound. Separate
reconstruction/signing tests must establish any stronger recovery claim; no new
importer is provided by this feature.

## Current evidence boundary and source attribution

| Synthetic case | Expected private keys / recovered roots | Current evidence |
| --- | --- | --- |
| pre-HD | 2 / 0 | Old DER/wkey/pool schema and native private/public checks |
| plaintext mixed | 2 / 0 | Complete synthetic records, script and label bytes |
| unsplit HD | 2 / 1 | v0.13.0 source semantics; independent root/child checks |
| split HD | 3 / 1 | v0.15.0 source semantics; receive/change checks |
| rotated seed | 4 / 2 | Active/inactive IDs and both root expectations |
| encrypted non-HD | 1 / 0 | v0.4.0 source-compatible uncompressed key; independent AES/KDF |
| encrypted HD | 4 / 1 | Seed, both chains and unrelated key validated independently |
| watch-only / empty / mkey-only | 0 / 0 | Public/empty inventory with no authority claim |

These are generator/oracle expectations. No row currently means a working
dumpwallet command, an authentic historic release-produced complete wallet,
successful historical application execution, restoration/signing proof, or a
universal supported release range. Release-specific whole-wallet claims remain
blocked on authentic fixture/oracle execution. No downloaded reference binary
is required by the mandatory fast fixtures.

Primary source is Bitcoin Core v29.5
`16ca532c8212d691daed14940da0f71f84f7b3fc`, src/wallet/rpc/backup.cpp's
EncodeDumpString/GetWalletAddressesForKey/dumpwallet, with current Roots
src/key.cpp, src/key_io.cpp, src/wallet/crypter.{h,cpp} and read-only
src/wallet/migrate.{h,cpp} primitives retained. The 19 selected
export/record/crypto/key/test blobs are identical in v29.4
`3fc0865963a38b871e9f7d94e6151c4953563516`; there is one modern donor.
Historical schema obligations are v0.18.1
`fa27a0760792b251585f2a70eccdd547f915b7e4`: src/wallet/walletdb.cpp ReadKeyValue,
src/wallet/wallet.h CWalletKey/CKeyPool and src/wallet/walletdb.h
CHDChain/CKeyMetadata; v0.13.0
`a402396dce64c42ea73535b7dde4a9164d430438` src/wallet/wallet.cpp GenerateNewKey/
GenerateNewHDMasterKey; v0.15.0
`3751912e8e044958d5ccea847a3f8eab0b026dc1` src/wallet/wallet.cpp DeriveNewChildKey;
and v0.4.0 `c7eb151ad0ed441d6fd598551059a9bbfb09e99e` src/db.cpp record handling
plus src/crypter.{h,cpp}/src/keystore.cpp encryption callers. The old private-key-IV comment is not the
oracle: executable callers use the serialized public-key hash.

Retain original Satoshi/Bitcoin/Core developer authorship and MIT notices in
adapted source. Do not transplant wallet engines, RPC/chain lifecycle, old
generation/write paths, obsolete OpenSSL code or Berkeley DB libraries. Source
pins were accepted from the official HTTPS repository for query-only inspection;
tag signer authentication remains unverified because the public keys were not
available. Source evidence and the adapted/current-primitive harness are
distinct from historical executable or release-artifact authentication.
