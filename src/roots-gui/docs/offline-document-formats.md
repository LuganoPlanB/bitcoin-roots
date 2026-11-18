# Offline document inspection

Open Transaction from File reads a chosen absolute local path. Open Transaction
from Text inspects a deliberate clipboard selection. Both routes validate content
on a bounded local worker. A file extension does not establish a format. Native
clipboard transport has its existing 64 KiB limit.

Supported formats are binary Bitcoin transactions, transaction hex, binary
Bitcoin PSBT v0 and v2, Base64 PSBT v0 and v2, and the supported public descriptor
forms. The display network is explicit. Bitcoin transaction and PSBT bytes do
not identify a network. Address strings are derived from their exact
scriptPubKeys using the chosen network. Descriptors require a supported network
and verified public keys and checksum when supplied.

The offline framing check uses a 64 KiB binary limit, 128 KiB plus 64 bytes of
source text or binary, 1,024 inputs or outputs each, and 4,096 PSBT entries across
all maps. Scripts, witness stacks and Taproot depth are separately bounded.
CompactSize lengths are canonical. Duplicate map keys, duplicate input
outpoints, truncation, trailing data and monetary overflow fail validation.
The pinned provider applies strict and complete PSBT decoding after these checks.
Unsupported PSBT versions produce an explicit version error.

Accepted files retain their original bytes, source encoding and decoded wire
bytes. This preserves field ordering, unknown and proprietary data, and exact
hex/Base64 text whitespace. Save Original writes a new local file and refuses
to overwrite an existing one. Wire Hex is an inspection view. It does not
convert a PSBT or replace the original representation.

Witness-only UTXOs are marked as supplied. A full prevout is used for offline
fee calculation only when its transaction ID and output index match and any
witness UTXO agrees. Conflicts and missing information remain visible. Matching
an exact script derived from a separately read wallet policy is script evidence
for that wallet and network. An imported fingerprint, wallet label, change flag
or proprietary field confers no ownership, funding or approval authority.

Opt-in recovery saves the original bytes and active representation within the
existing workspace size limits. A fresh process revalidates those bytes before
opening an offline tab. No profile, approval or action resumes from the document.

## Cached offline drafts

Use Read saved descriptors in wallet Settings before selecting exact inputs in
UTXOs if the draft will need change. Send Selected captures bounded public
prevouts and the verified internal change descriptor when the wallet observation
and local coin policy are current. It neither contacts a signer nor allocates a
change address. With public recovery enabled, the exact pinned set and its
public evidence survive restart.

Construct offline opens the cached input editor. Enter a recipient address,
amount and fee in exact BTC units. Review the cached change address and opt in
if the inputs exceed the recipient and fee. Previous outputs, known exclusions
and change are verified before construction. An unavailable input blocks the
whole pinned set. No coin is substituted. The snapshot time is visible. Current
spendability, unused change status and relay eligibility remain unknown offline.

Save Original exports the unsigned PSBT. An external signer can return a PSBT
in the same version with partial signatures. The merge compares the exact
ordered underlying transaction, rejects contradictory fields, authenticates
the supported scripts and verifies each new signature. Full previous
transactions are required, even when a witness output is present. ECDSA
requires SIGHASH_ALL. Taproot key-path accepts SIGHASH_DEFAULT or SIGHASH_ALL.
Supported policies include single-key legacy, native and nested SegWit,
standard legacy and SegWit multisig through 15 keys, and Taproot key path.
Finalized external inputs and Taproot script-path signing are outside this
workflow.

A fully satisfied PSBT can be finalized locally into a separate raw transaction
tab. Original and merged PSBTs remain available. Save Original exports the raw
transaction without broadcast. A fresh online wallet observation is needed
before any later transaction action.

Record Local Review compares a constructed draft with the independently cached
wallet policy and the exact payment plan. It requires matched full previous
outputs, an exact fee, and a separately derived internal script for any change.
Origins in the imported PSBT are only hints. Live spendability and relay status
remain unknown. Local review is held in the current session and grants no
signing, reservation or broadcast authority.

The unsigned transaction commitment covers ordered outpoints, sequences,
amounts, scripts, version and locktime. Equivalent PSBT v0 and v2 transactions
have the same commitment. Review additionally compares proved previous outputs,
sighash modes, script policies and modification permissions. Map order and
opaque metadata do not change that commitment. A relevant change rejects the
merge and shows why the local review was revoked. Merge still operates on the
working PSBT version and retains the original source bytes.
