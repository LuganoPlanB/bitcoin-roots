# Standalone public-data protocol provider

The adopted dependency contract is libwally-core **1.6.1**, commit
`f7b0a15b8d6fc79a5c4ed0123955dd482934483e`, with secp256k1-zkp
`8e1f96c20e16bf960be6caec7f3c94acb360cdf2` and the recorded PSBT conformance
patch. `third_party/provider-lock.json` binds all local archives, source URLs,
licenses and patch hashes. Upstream tag/commit identities were obtained over
HTTPS; upstream signature trust was not independently established. No library
name, tag signature badge or configuration success substitutes for API tests.

The unmodified candidate failed three official parser vectors: BIP174's
incorrectly sized embedded transaction and two BIP370 reserved-modifiability-bit
examples. The bounded alternative is the same pinned provider plus a separate,
reviewable correction, not another crypto implementation. The patch checks the
canonical embedded transaction length while preserving partial/pre-BIP144
serialization, retains reserved imported BIP370 bits without granting additional
permissions, and moves the upstream obsolete reserved-bit rejection fixture to
valid coverage. Generated `psbts.h` is regenerated from the corrected JSON.
No signature, hash, curve or key-generation algorithm is changed. Original
upstream source bytes remain in the archive; the patch is applied only in staging.

## Proven API boundaries

| Contract | Provider API / evidence |
| --- | --- |
| Network-aware public addresses/descriptors | `wally_descriptor_parse`, `descriptor_to_script`, `descriptor_to_address`; static/ranged, nested segwit, multisig and Taproot tree derivation; exact address parity against Roots regtest |
| Public unsigned construction | `wally_tx_init_alloc`, add raw inputs/outputs, `wally_psbt_set_global_tx`, PSBT version conversion; no key input |
| PSBT v0/v2 parsing | STRICT + COMPLETE flags, never LOOSE; official BIP174/370/371 valid/invalid vectors; unsupported versions remain errors |
| Unknown/proprietary preservation | Unknown global/input/output maps survive parse/serialize, version change and merge; BIP370 reserved flag bits survive same-version serialization |
| Public signature verification | Provider signature-hash APIs, DER conversion and `wally_ec_sig_verify`; ECDSA and BIP340 Schnorr; wrong output amounts invalidate signatures |
| Prevout/script evidence | Full non-witness transaction ID verification, matching witness/non-witness facts, script/redeem/witness hash commitments and multisig key membership |
| Merge | `wally_psbt_combine`, after independent full transaction/prevout/policy identity checks; happy-path combination vectors preserve expected metadata |
| Finalization | `wally_psbt_finalize`, explicit finalized-status check and extraction; legacy, segwit, Taproot keypath and multisig vectors; incomplete returns are not completion |

The public signature probe checks partial signatures, not threshold completion.
The application must count satisfaction for every input's exact policy. The
initial supported standard script families are P2PKH, native/nested P2WPKH,
P2SH/P2WSH/nested P2WSH standard multisig up to 15 keys, and Taproot keypath.
The provider's finalizer has that explicit 15-key bound; reject larger policies
with a supported-policy explanation instead of treating them as satisfied. Public Taproot
tree descriptors and BIP371 metadata are inspectable; arbitrary tapscript or
Miniscript satisfaction is not supplied by the provider's PSBT finalizer.
Such a policy must remain visibly unsupported for local finalization rather
than be treated as complete. The required offline journeys for declared
supported policies have a complete dependency API; no daemon/GUI key custody
or Core-internal fallback is required. Application adapters and journeys remain
owned by the offline/signing milestones, not implemented by these adoption probes.

The raw provider is not an approval authority. In particular, its BIP370 PSBT
identifier used for combination intentionally omits some locktime/sequence
facts. Never use that identifier as `ApprovalIdentity`. Compare actual ordered
unsigned transaction fields plus prevouts, policy, sighash and change proofs
before merge, and reject contradictory metadata. Unknown prevouts stay Unknown.
Raw finalization assembles scripts; it does not prove that supplied signatures
are cryptographically valid. Validate them before merge/finalization and refresh
final-byte txid/wtxid, size, vsize, fee and fee rate afterward. Importing an
already-finalized container does not waive verification.

## Reproduce from local inputs

Required developer tools: CMake >=3.22, Ninja, C/C++20 compiler, Python >=3.10
and Git for applying the local patch. Use ccache when present and `-j 2`.
The normal provider preparation/build/test performs no source retrieval.

```sh
python3 src/roots-gui/third_party/prepare_provider.py --destination /absolute/out/provider-input
cmake -S /absolute/out/provider-input/libwally-core-1.6.1 -B /absolute/out/provider-build -G Ninja \
  -DWALLYCORE_BUILD_ELEMENTS=OFF -DWALLYCORE_ENABLE_TESTS=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build /absolute/out/provider-build -j 2
cmake -S src/roots-gui/tests/protocol -B /absolute/out/provider-probes -G Ninja \
  -DROOTS_GUI_PROVIDER_SOURCE=/absolute/out/provider-input/libwally-core-1.6.1 \
  -DROOTS_GUI_PROVIDER_BUILD=/absolute/out/provider-build
cmake --build /absolute/out/provider-probes -j 2
ctest --test-dir /absolute/out/provider-probes --output-on-failure --no-tests=error
```

The probe CTest entry installs a process-local Linux seccomp filter and proves
IPv4/IPv6 socket creation fails with EPERM. The executed qualification also
wrapped fresh preparation/configuration/build and upstream tests with that
runner, using a new empty compiler cache. The archive includes the exact secp
submodule, so no checkout, network fetch or dependency cache is required.
Namespace creation was unavailable; seccomp only reduces child permissions and
never changes host networking. Other hosts are unverified and fail explicitly.

Upstream's CMake registers `test_elements_tx` even with Elements disabled.
The Bitcoin profile runs the seven applicable suites with
`-E '^test_elements_tx$'`; the initial profile-mismatch failure is retained.
This is an explicit out-of-profile test, not a skipped Bitcoin assertion.
Current direct tests include official parser/signature vectors, public signature
and mutation checks, descriptor derivation, merge, finalization and unknown data.
Preserve `Testing/Temporary/LastTest.log`: CTest's default JUnit output truncates
the inner vector report. Zero execution or missing offline enforcement fails.

ImKit hosting and normal consumer targets are integrated by the build milestone.
The provider's upstream CMake assumes it is the top-level project; isolate that
build and import uniquely named GUI targets instead of polluting parent Core
settings or colliding with its secp target. Neither Core libraries nor Qt are
linked by the standalone provider probe.

## Update and redistribution

Updates require explicit reviewed source changes, renewed API/vector/offline
qualification and notice review. Acquire an exact upstream release and its exact
submodule into disposable scratch, inspect changes/licenses, then run
`third_party/vendor_sources.py --wally-checkout PATH --bips-input PATH` as a
maintainer operation. It packages committed Git objects, not dirty probe edits.
Use the BIP revision/URLs in the lock file, inspect every resulting archive/hash
and update the patch deliberately. Normal CMake never calls this updater.
`third_party/NOTICES.md`, complete license texts and embedded upstream copyright
files accompany redistribution. No install, release or publication is implied.
