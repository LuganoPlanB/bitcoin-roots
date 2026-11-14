# Offline legacy wallet export

`bitcoin-wallet dumpwallet` exports a supported legacy wallet offline. This
guide describes the implemented version 1 format and its limits; it does not
establish a released `v32.1-roots.2` or universal historical compatibility.
The [evidence boundary below](#current-evidence-boundary-and-source-attribution)
distinguishes synthetic cases, exact-release comparisons and unverified platforms.

Unlike the wallet tool's raw `dump` of database records, this command exports
decoded private keys, positively identified HD
seeds and BIP32 roots, scripts and recovery metadata from a stable offline
legacy BDB copy. Keep the original wallet backup. The export contains
unencrypted spending material when private keys are present; its metadata is
private information too. It does not preserve transaction history, a balance,
chain state, a BIP39 mnemonic, or every importer's wallet state. An HD root does
not cover unrelated imported keys. Every recoverable individual key is exported
even when roots are available. A non-HD wallet has no root.

## Export an offline copy

1. Retain the original wallet backup independently. Use a stable offline copy
   made after clean shutdown; do not point this command at a running wallet.
2. Prepare an existing empty datadir and a separate, owner-controlled output
   directory. On POSIX, the output parent must belong to the current user and
   must not be writable by group or others. The output must be outside the
   source wallet directory. On Linux, both parents must be in the same mounted
   view; see [publication limits](#publication-status-and-recovery-limits).
3. Choose the encoding network explicitly and choose a new output filename.
   The following paths are placeholders for those prepared locations. Keep
   shell output on the terminal, away from both the source and export.

```sh
# Plaintext, or encrypted with a non-echoing controlling-terminal prompt.
bitcoin-wallet -datadir=/path/to/empty-datadir -chain=main \
  -wallet=/path/to/offline-copy/wallet.dat \
  -dumpfile=/path/to/private-output/wallet.keys dumpwallet

# A trusted parent supplies the passphrase through stdin, never in this command.
bitcoin-wallet -datadir=/path/to/empty-datadir -chain=main \
  -wallet=/path/to/offline-copy/wallet.dat \
  -dumpfile=/path/to/private-output/wallet.keys -stdinwalletpassphrase dumpwallet
```

4. For the first command, enter an encrypted wallet's passphrase only at the
   tool's prompt. On POSIX, finish with **Ctrl-J**, which supplies LF; ordinary
   Enter may supply CR and does not finish collection. Terminal line editing is
   disabled, so Backspace is not an editing command. Ctrl-C cancels the export.
   For automation, use the second command only with a trusted parent supplying
   the passphrase directly on stdin. Never put it in argv, a shell literal,
   environment, config entry or command history.
5. Check the exit status and reported state/counts. Exit 0 means the complete
   file was published, including `# roots-export-complete: 1`; it does not mean
   a balance or restored wallet was verified. Protect the export as spending
   material and retain the original backup. If reporting fails after publication,
   inspect the destination before retrying; an existing file is never replaced.

The same invocation handles pre-HD, mixed/imported-key, HD and seed-rotated
inventories. A watch-only export has zero private keys and roots and cannot
spend. Only add `-stdinwalletpassphrase` when encrypted private keys require it.

Decryption failures identify their stage without reporting key data:

- `master-key stage`: no candidate decrypted to the required 32-byte length.
  The entered passphrase bytes or encrypted master-key material need investigation.
- `private-key stage`: a master candidate decrypted, but no encrypted private
  key passed decryption and public-key verification. A decrypted candidate alone
  does not prove that the passphrase is correct for the required keys.
- `incomplete private-key recovery`: some encrypted keys verified and others
  did not. The tool rejects the export rather than publishing a partial result.

All three are failures with no new export published. These diagnostics narrow
the failing operation; they do not by themselves establish corruption, a wrong
passphrase or universal format compatibility.

## Invocation and input rules

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

Keep stdout and stderr separate from both source and destination, including
hardlink aliases. A shell can create or truncate redirected files before this
process runs. Before startup errors, help or version reporting, the command
checks only filesystem identities for recognizable supplied source/destination
paths, including all duplicate candidates in rejected invocations. It suppresses
inherited report channels that alias those files; an identity-inspection failure
suppresses unverified regular-file channels. It does not read wallet contents or
initialize a wallet/datadir for this protection. Ordinary pipes, terminals and
nonaliased file logs remain available. It repeats checks around source admission
and before export; pathname checks alone cannot make malicious concurrent path
swaps safe. Use a stable offline copy and preserve the original.

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
through, with CR-to-LF translation disabled: use Ctrl-J to terminate the line.
Native Windows console Enter ends collection as a console event; a trailing
data CR remains part of the passphrase. Console Unicode is encoded as UTF-8 without normalization,
rejecting invalid surrogate input. Explicit stdin remains a raw byte path. The
same representable input must reach crypto as identical bytes in either mode.
Do not retry altered bytes, old NUL-truncated variants, or another passphrase.

## What the exported secrets mean

| Material | Meaning and recovery limit |
| --- | --- |
| Ordinary WIF | One private key with its compression flag and selected-network encoding. It may be imported, reserve or unclassified; its presence does not make it an HD seed. Keep every exported WIF, including keys unrelated to a root. |
| WIF-encoded seed | The original private bytes of a positively identified Core HD seed, also exported as a key payload. It is not a BIP39 mnemonic. Identification and active/inactive status appear in seed metadata. |
| BIP32 root (`xprv`/`tprv`) | A root derived from those seed bytes. Use the recorded paths and origins; the supported Core receive/change paths are `m/0'/0'/i'` and `m/0'/1'/i'`, not an assumed BIP44 path. Every recoverable identified seed gets a root, including inactive seeds. A root alone does not recover unrelated imported keys. |
| Symmetric `mkey` | A passphrase-encrypted key used to decrypt stored private keys. It is not a BIP32 root and is never exported as one. The export retains only its public parameter/verification metadata. An mkey-only wallet proves no recovered spending authority. |

A non-HD wallet exports its individual keys and has no seed/root. Public-only
inventories can retain unresolved seed IDs with null roots. Missing required
seed material in an inventory containing private keys instead fails the whole
export; there is no partial-success mode.

## Supported and rejected inputs

| Input | Result within version 1 limits |
| --- | --- |
| Clean legacy BDB v9, supported records | Export validated plaintext or decryptable keys, stored scripts and metadata. Pre-HD, unsplit/split HD, active/inactive seeds and unrelated imported keys are covered by the specific cases below. |
| Watch-only, empty or mkey-only inventory | Export available public metadata with zero private keys/roots; no password-verification or spending-authority claim. |
| SQLite/descriptor wallet, non-BDB file, built-in BDB encryption or dirty LSNs | Reject. Use a suitable wallet tool for that format; this command does not migrate, repair or recover a dirty database. |
| Unknown record family, incompatible flags, damaged required key/script, missing required private seed or resource excess | Reject without publishing a partial export. A limit error is not proof of corruption or a wrong passphrase. |

Release names alone do not establish support. The exact storage geometry,
record allowlist, resource limits and executed evidence follow.

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
| `keymeta` | Exact metadata versions 1, 2, 10 or 12. v1 has version/time; Knots v2 adds one opaque `unsupported_key_flags` byte; v10 adds byte path/seed ID instead of that flag; v12 adds four-byte origin fingerprint, uint32 path vector and has-origin bool. Preserve raw fields and unknown times without interpreting the Knots flag. Other versions fail rather than inferring their production from donor `>=` conditions. |
| `pool` | int64 index and stream-version/time/pubkey, followed by zero, one or two bool bytes (internal, pre-split). Preserve field presence; absent bools default false. Each present bool must be 0 or 1. No keypool refill/consumption. |
| `cscript` | Require script hash160 key to match the serialized script; validate push-data boundaries. Export redeem-script payload and metadata. A valid empty script is retained only in typed metadata because the legacy importer cannot recognize an empty HEX token. Do not require the script to be standard or independently spendable. |
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
Watch-only scripts without cscript appear only in typed metadata. An empty
cscript also appears only in typed metadata, with `script_hex=""`, its redeem
kind, hash/time associations and script count preserved; it has no legacy
payload line. No nonempty substitute is emitted. The one-second
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
- **Metadata**: object members `version` (1/2/10/12), `created` (raw Time64),
  `unsupported_key_flags` (JSON integer 0–255, present only for Knots v2),
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

On Linux, source and destination parents must be in the same mounted view.
The exporter retains both directory handles and requires matching `statx`
mount IDs, returned type/inode masks and identities matching `fstat`, initially
and again before publication. Missing capability or different mount IDs fails
closed, even for an otherwise writable destination on another filesystem or
bind mount. This conservative restriction requires Linux `STATX_MNT_ID` support
(introduced in Linux 5.8); it avoids treating a subtree or whole-filesystem bind
alias as proof that output is outside the source tree. It does not alter the
read-only database reader or migration. Mounted alias scenarios still require
native qualification; no Linux mount-ID proof is claimed for macOS or Windows.

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

Ordinary exceptions and cooperative cancellation unwind invocation-owned output
and controlled encoded-secret buffers before returning failure. Cancellation is
observed between bounded processing operations, not inside a running KDF or
cryptographic primitive. Deletion removes a directory entry; it does not erase
filesystem journals, snapshots, backups, SSD remapping or previously written
blocks. Cleanup requires the filesystem to remain accessible and permit removal;
an unhandled termination, SIGKILL or power loss cannot run cleanup. A surviving
private temporary file is never selected or imported automatically.

Secure allocators cleanse their owned allocations on release, and the renderer
cleanses its controlled ordinary xprv value without creating serialized JSON or
stream scratch containing it. This is not total memory erasure: caller-owned
passphrases, ordinary metadata strings and their intermediate allocations,
primitive stack/register copies, allocator behavior, swap and crash dumps
remain outside that guarantee. Keep host storage, swap and dump
policy appropriate for unencrypted spending material.

Exit0 means the complete file was published. Success stdout contains only the
destination, state and non-secret counts; warn about unencrypted spending
material when private_keys is positive. Other outcomes use the tool's ordinary
nonzero failure path. Diagnostics identify categories/record counts, not
passphrases, private keys, roots, raw records, labels or arbitrary argument
values. Missing/corrupt required data, wrong passphrase, unsupported formats,
unknown families/flags, limits, detected source changes, aliases, races and
write/permission failures cannot be reported as partial recovery success.

After publication, a closed, broken or suppressed report channel does not undo
the export or turn it into an export failure. The process retains committed
success and attempts a fixed warning on stderr; if both channels are unavailable
even that warning cannot be delivered. Inspect the requested destination. No
reporting failure authorizes deletion, overwrite or automatic retry.

### What Core importwallet restores

The tested Core v29.5/v0.18.1 `importwallet` consumers read payload lines and
ignore comments. Keep the complete export for separate reconstruction of its
metadata; importing it is not a complete wallet restore.

| Export content | Core importwallet behavior / remaining work |
| --- | --- |
| Individual WIF and nonempty redeem-script payloads | Import the keys/scripts. A legacy label token does not restore every original destination-label association; `unclassified=1` does not restore the original classification. |
| `# extended private masterkey` and seed `xprv` fields | Ignored as comments. They do not configure the destination's HD chain or recover inactive/active root relationships. Preserve them for a consumer that understands the recorded derivations. |
| `roots-meta-v1` key/hdchain/pool/address/encryption/omitted objects | Ignored as comments. Original paths, origins, keypool state, raw labels/purposes, wkey expiry/comments and encryption-parameter metadata are not restored. |
| Watch-only and empty redeem-script metadata | Ignored as comments. An empty script has no importable payload token; these public scripts require separate reconstruction and do not themselves confer signing authority. |
| Unknown-time epoch fallback | Requests the earliest conservative scan bound. History discovery still needs independently available blockchain data and a rescan by the receiving tool; the export supplies neither blocks nor saved transaction history. |

Settings, spent/locked state, receive requests, ordering and transaction history
are omitted from the export, as described in the record allowlist. A balance
must be established independently. Separate reconstruction/signing tests must
establish any stronger recovery claim; no new importer is provided by this
feature.

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

The mandatory `tool_wallet_legacy_export.py` matrix exercises all ten synthetic
families through the offline command at all eight supported BDB page sizes in
both byte orders, plus two chained-overflow script cases (162 cases). It checks
complete WIF, active/inactive root and script inventories, original metadata
versions 1/10/12, old/split/pre-split pool fields, wkey metadata and source
preservation. Expected roots use the independent Python BIP32 implementation;
metadata expectations read the original serialized records independently of the
C++ exporter. Changing the BDB geometry must preserve every non-wallet metadata
and payload byte. The native exporter suite separately exercises the frozen
record corpus and deliberate rejections.

Knots [v27.1.knots20240801's metadata serializer](https://github.com/bitcoinknots/bitcoin/blob/e933c45607ca9eb50fa3d17c6566e6fb8e6b21e1/src/wallet/walletdb.h)
defines the additional v2 layout. Separate synthetic cases check its opaque
flag byte for plaintext, encrypted and watch metadata, malformed lengths,
conflicting duplicates and unchanged source bytes. This is schema-based
compatibility testing; it does not establish testing of a personal wallet or
every Knots wallet feature.

A header survey of 25 Core major-line releases (v0.8.0 through v32.1) and
41 non-RC Knots release tags identified only metadata versions 1, 2, 10 and
12. Those four exact layouts are supported. This source survey does not mean
every release-produced wallet has been exercised; unassigned version numbers
remain rejected rather than being treated as compatible by guesswork.

These synthetic/source comparisons establish specific format cases, not a
universal supported release range or proof that a named release emitted each
complete wallet. The extended `tool_wallet_legacy_export_compatibility.py` test
requires exact v29.5 and v0.18.1 binaries for release-produced wallet comparisons;
an unavailable or skipped reference test does not establish that coverage.
On the executed x86_64 Linux reference profile, both releases produced plaintext
mixed and encrypted seed-rotation wallets; v29.5 additionally produced watch-only
and empty wallets (six cases). Each original wallet was unloaded cleanly, copied
offline and captured as complete raw records before export. Complete WIF sets
and active roots matched the corresponding release's dumpwallet. Every inactive
root was derived independently from the donor's seed WIF. Both plaintext and
encrypted mixed cases retained the unrelated imported key and multisig script.
This is evidence for those cases at those exact versions, not for every wallet
created by those releases. The binary archives were accepted by official HTTPS
origin and exact published SHA256; OpenPGP signer authentication and reproducible
build equivalence remain unknown. The downloader currently pins these additional
reference archives for x86_64 Linux only; other native platforms need their own
approved artifacts and execution.

One intentional script difference is checked explicitly. Core's live keystore
adds a P2WPKH wrapper for every compressed key through
`ImplicitlyLearnRelatedKeyScripts`, including scripts never stored on disk
(v29.5 `src/script/signingprovider.cpp`, v0.18.1 `src/keystore.cpp`). Its dumpwallet
therefore includes these in-memory entries. This offline command exports stored
`cscript` records. The reference test requires exact equality with the stored
script set and requires the donor's complete set to equal that set plus the
independently derived wrappers; it does not discard arbitrary donor differences.
The versioned metadata envelope, all identified inactive roots, conservative
unknown-time fallback, neutral unclassified token and absence of live-chain
headers are the other deliberate differences described above.
No downloaded reference binary is required by the mandatory fast fixtures.
Restoration/signing authority is a separate test boundary.

The extended `tool_wallet_legacy_export_recovery.py` exercises that boundary
with the same exact v29.5/v0.18.1 references. Independent Python parsing of the
exported WIFs and root, BIP32 receive/change derivation and public-key checks
precede signing. A mixed encrypted fixture contains an unrelated imported key
and a 2-of-2 multisig requiring that key and a derived receive key. Material
derived from the root at the recorded paths cannot complete the imported-key
or multisig spend; the exported individual WIF supplies the missing authority.
The test checks three message signatures and four spend templates against the
reference script verifier. Four clean wallet imports (full file and payload
alone in each release) reproduce the complete WIF inventory and independently
sign those same four templates: 20 script verification checks in total, not
20 distinct transactions or script policies.

The test also separates history discovery from saved history. A funding
transaction precedes the final chain tip by almost a day. Unknown-time payloads
use the conservative one-second epoch; both reference importers scan all 104
test blocks and find that transaction. Controls with recent payload timestamps
scan only the last block and miss it. The export itself contains no transaction
history; discovery requires the reference node's independently supplied blocks.
Full and payload-only imports both omit original HD paths, origins, active-seed
and keypool relationships, confirming that Core ignores the comment metadata.
Separate public reconstruction of watch-only script metadata remains unable
to sign. These checks establish the tested legacy P2PKH/2-of-2 P2SH capabilities;
they are not a general metadata importer, a guarantee for arbitrary scripts or
evidence of a mainnet balance.

The native Linux failure checks identify 57 end-to-end groups: ten successes
from read-only source directories with Unicode/space-containing paths, 16
negative input cases repeated with writable and read-only source directories,
one unreadable-source case, eleven destination alias/permission cases, and
three terminal-gated source/destination races. The negative matrix includes
malformed key records/checksums/pool flags/scripts, unknown families/incompatible flags,
wrong credentials, missing seeds, corrupt encrypted keys with valid checksums,
dirty BDB input and unsupported outer/inner BDB encryption. Debug/console
options are enabled for the table; per-case diagnostics are captured and checked
against synthetic key/root/label/credential canaries. Content, identity, mode,
size, mtime and sibling snapshots must remain unchanged; atime is excluded
because both exporter and verification reads can update it. Existing files
remain intact and handled failures leave no invocation-owned temporary output.

The controlled CLI races observe the passphrase gate, inject their change,
release the gate and await bounded process completion; source and destination
states must then equal the injected states. Separately, the existing native
`output_destination_races_never_replace` test inserts each of five destination
types at the actual publication stage: regular file, source symlink, dangling
symlink, directory and source hardlink. It exercises the native no-replace move
and preserves every competing entry. These executed Linux checks do not qualify
native Windows/macOS permissions or mounted-media scenarios.

Fuzz development and execution are deferred for this delivery; fuzz coverage is
unverified. An experimental harness attempt stopped at the fuzz framework's
global-randomness guard. Its harness setup was corrected and built successfully,
but no replay or subsequent campaign verified that repair. The experimental
target is excluded from this delivery; its source, corpus and failed-run evidence
are preserved separately. This attempt does not establish a production sanitizer
defect or successful fuzz coverage.

The focused exporter unit and functional cases were also executed with Clang 19
AddressSanitizer and UndefinedBehaviorSanitizer on x86_64 Linux. That evidence
covers the tested code and cases only. Native macOS and Windows permission/input
checks and mounted-media qualification remain unverified prerequisites; the
Linux checks above do not establish those platforms' behavior.

### Maintenance provenance

The pins below identify the inspected upstream sources. The implementation
adapts small payload/schema helpers and retains current Roots crypto and BDB
reading primitives; it does not transplant an operational legacy wallet engine.
The following map names the maintained adapters and their regression evidence.
Unit case names refer to
[`legacy_wallet_export_tests.cpp`](../src/wallet/test/legacy_wallet_export_tests.cpp).

| Upstream source and symbol | Current adapter and intended differences | Differential/regression checks |
| --- | --- | --- |
| [v29.5 backup.cpp](https://github.com/bitcoin/bitcoin/blob/16ca532c8212d691daed14940da0f71f84f7b3fc/src/wallet/rpc/backup.cpp): `EncodeDumpString`, `GetWalletAddressesForKey`, `dumpwallet` | [`legacy_export_format.cpp`](../src/wallet/legacy_export_format.cpp): `WriteDumpString`, `MatchAddresses`, `Render`. Preserve payload byte escaping/association; add deterministic metadata, all identified inactive roots and conservative unknown times. Remove RPC, chain synchronization, live birthdays and block headers. Export stored scripts rather than the donor's in-memory implicit wrappers. | `format_frozen_corpus_all_networks`, `format_labels_associations_and_injection`; [`tool_wallet_legacy_export_compatibility.py`](../test/functional/tool_wallet_legacy_export_compatibility.py) compares exact-release WIF/root/stored-script sets. |
| [v0.18.1 wallet.h](https://github.com/bitcoin/bitcoin/blob/fa27a0760792b251585f2a70eccdd547f915b7e4/src/wallet/wallet.h): `CWalletKey`, `CKeyPool`; walletdb.cpp `ReadKeyValue`; v29.5 walletdb/schema loaders | [`legacy_export.cpp`](../src/wallet/legacy_export.cpp): `HistoricalWalletKey`, `ReadPool`, `DecodeLegacyWalletRecords`. Preserve historical fields and their presence; require bounded, complete records and consistent identities instead of wallet mutation or permissive partial loading. | `record_fixture_inventories`, `record_public_metadata_and_pool_versions`, `record_checksums_and_semantic_conflicts`; independent donor/current-primitive serializer vectors and the synthetic geometry matrix in [`tool_wallet_legacy_export.py`](../test/functional/tool_wallet_legacy_export.py). |
| [v0.13.0 wallet.cpp](https://github.com/bitcoin/bitcoin/blob/a402396dce64c42ea73535b7dde4a9164d430438/src/wallet/wallet.cpp): `GenerateNewKey`, `GenerateNewHDMasterKey`; [v0.15.0 wallet.cpp](https://github.com/bitcoin/bitcoin/blob/3751912e8e044958d5ccea847a3f8eab0b026dc1/src/wallet/wallet.cpp): `DeriveNewChildKey`; later `m`/`s` metadata semantics | `DeriveLegacyWalletRoots` and `ValidateLegacyWalletCoverage` in `legacy_export.cpp`, using current `CExtKey`/encoding primitives. Interpret stored seed/path evidence and verify children; preserve all individual/imported keys. Never generate keys, rotate seeds or reconstruct counters. | `roots_independent_fixture_oracles`, `roots_published_bip32_vector_four`, `coverage_unsplit_and_child_index_boundaries`; independent Python roots for unsplit/split/rotated synthetic cases. |
| [v0.4.0 db.cpp](https://github.com/bitcoin/bitcoin/blob/c7eb151ad0ed441d6fd598551059a9bbfb09e99e/src/db.cpp): `CWalletDB::LoadWallet`; crypter/keystore encryption callers; v29.5 key recovery | `RecoverLegacyWalletSecrets` retains current `CCrypter`/`DecryptKey`, verifies every required private/public pair and bounds KDF work. Keep the serialized-public-key hash IV behavior; remove unlock, upgrade and writable lifecycle paths. Never export the symmetric master key. | `recovery_independent_ciphertext_vectors`, `recovery_exact_passphrase_bytes`, `recovery_no_partial_success`; [`tool_wallet_legacy_export_recovery.py`](../test/functional/tool_wallet_legacy_export_recovery.py) separately tests the qualified import/signing boundary described above. |

Historical source/vector checks and exact-release wallet comparisons are distinct
evidence. The test names above are maintenance entry points, not broader support
claims. In particular, inactive roots, typed comment metadata and retained WIFs
do not provide a general importer or saved transaction history.

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

Copyright (C) 2026 Plan-B Foundation
