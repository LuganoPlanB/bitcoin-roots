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

## Transaction workbench

Choose Overview, Inputs or Outputs in the structure pane. Large transactions
use numbered pages. Overview shows the exact output total, script distribution,
transaction version and locktime. The input amount and fee stay Unknown when
matching previous-output evidence is incomplete. An output worth zero
satoshis still shows its exact value.

Input and output details identify scripts, timing constraints and any available
wallet evidence. Previous-output data supplied in a PSBT is labelled separately
from an amount or script matched to the actual previous transaction ID. An
independently saved internal change proof establishes intent without asserting
address allocation or live spendability. Spending history requires current
observations for the exact wallet and network. Missing observations leave it
Unknown.

Show wire hex opens the byte drawer. Original source keeps the exact submitted
text or binary bytes. Decoded PSBT shows the binary container, including opaque
fields. Raw wire shows transaction bytes, including any scriptSig or witness.
Derived unsigned is a separate PSBT serialization without final scriptSig or
witness. Each byte field maps only to the active representation. Base64 source
text cannot directly map binary fields. Switch to Decoded PSBT to see their
offsets.

Copy renders the selected bytes as hexadecimal text. Save writes the exact
selected bytes, or the complete active representation when no range is
selected, to a new file. Save original still writes the exact source. A raw
transaction can show its txid and wtxid. A PSBT can show an unsigned
commitment while the signed transaction ID remains Unknown.

## Review and approval for signing

Signing review shows the exact wallet, ordered payments, independently proved
change, inputs, fee and locktime. It separates the requested funding rate from
the final fee rate, which requires a signed transaction size. A PSBT without
independent payment and wallet evidence can be inspected and exported. Open
Wallet starts the ordinary wallet chooser so you can select the exact wallet.

Record signing review and Approve for signing are separate actions. Approving
records consent for the exact reviewed transaction, source bytes and wallet
proofs in the current workspace session. A changed output, draft revision,
proof, wallet, policy or lock/session context retires that review. Export and
public recovery preserve the transaction bytes. Review and approval need
fresh actions after restart.

Advanced sighash explains which inputs and outputs a signature covers. An
explicit Taproot ALL request needs a separate acknowledgement. Unsupported,
unknown or conflicting sighash modes block approval.

After approval, Enrich PSBT requests public data without signing or finalizing.
Sign with daemon is a separate action for the exact compatible wallet and
approved PSBT. The daemon reply must pass local previous-output and transaction
checks, and any new signatures must validate against the approved commitments.
Open verified signed PSBT displays an exportable copy. The reviewed original
remains available in its own tab. Daemon completion and derived transaction hex
do not advance the workflow.

An encrypted daemon wallet has an explicit 30-second unlock action. The masked
passphrase is transient and is omitted from accessibility values, metadata and
diagnostics. Unlock requires TLS or a direct numeric loopback connection. The
daemon owns timeout and relocking. Unlock never starts signing. Cancelling or
changing wallet, profile or lock context discards the response locally, even if
the daemon already accepted the request.

## Collecting external signatures

Use Merge signed file for a local binary or Base64 PSBT, or Merge signed text
for Base64 PSBT text placed on the native clipboard. Copy PSBT text exports
Base64 without changing any decoded records. Save original preserves the
opened representation. From an approved review, Save collected PSBT exports
the current validated collection separately from the approved original.
The native clipboard is bounded. Use a file for larger PSBTs.

QR exchange accepts a single local PNG containing one readable code or decoded
QR text entered explicitly. Static PSBT codes carry canonical Base64. Multipart
PSBT codes use the indexed systematic subset of UR v2 `psbt` (untagged CBOR
byte string, minimal Bytewords and CRC32). Export repeats the indexed frames so
missed frames can be scanned again. Shuffled frames and exact duplicates are
accepted. Mixed fountain fragments, other UR types and BBQr are unavailable.
The checksum distinguishes ordinary mixed streams and corrupted data. It grants
no signing authority. Every assembled PSBT goes through the strict PSBT parser
and, in signing review, the existing exact approval and signature verifier.

Exports show the encoding, outbound byte count, matrix dimension and frame
number. Density changes the size of indexed fragments. You can pause, step or
restart a multipart display. Export omits global extended public keys by default
in a separate copy. Include them if a signer requires that metadata. All other
records, including proprietary data, per-input derivations and signatures, are
retained. The original document and approved collection stay intact.

The current limits are 16 KiB of PSBT data, 64 indexed frames, two minutes per
assembly/display, 780 decoded characters per symbol and a 4 MiB PNG with sides
up to 1536 pixels. PNG import checks the exact filtered scanline length,
including Adam7 passes, in a fixed-size inflater before decoding pixels.
Unexpected inflated bytes are refused. Decoder allocation charges are capped
at the smaller of 64 MiB or 64 KiB plus 24 bytes per pixel and three times the
PNG size. Concurrent calls share a 96 MiB cap, including luma buffers until
released. Replacement allocations charge both old and new memory until the
copy finishes. Display uses at least three physical pixels per module and a
four-module quiet zone. Choose a file if a PSBT cannot fit at a supported
density. Receive can display an address or payment URI. Import a payment QR
from Send to preview a destination and explicitly apply it to the current
recipient. QR data cannot change the active chain, contact a URL or submit a
payment. A bare PSBT does not encode its chain. Check the display network and
payment proofs before approval. Raw transaction QR display is static and
bounded. The PSBT QR import route refuses raw transactions. Linux camera import can feed the same validators. A decoded frame merely
reports detection. A complete parsed payload stays staged until the user
chooses Use this payment request or Import validated PSBT. The current
document, wallet, stream and approval gate that action. Physical signer
interoperability has a separate qualification.

The signature view checks every input against a matching full previous
transaction and the exact supported script. It reports verified public keys
per input. A 2-of-3 policy is satisfied only after two valid signatures for that
input. Complete requires every input to be satisfied. Missing evidence,
unsupported policies or unverified signatures are shown with their reason.
Public keys in this view identify signature checks, not devices or people.

Merge preserves exact transaction and policy identity and all nonconflicting
original records. It refuses invalid signatures, conflicting records and
imports without new verified signatures. Compatible v0 and v2 transactions
can be compared. Merge requires the same PSBT version on both sides and never
converts or strips maps to make them fit. Signature-only material can stay with
the exact live approval after independent proof checks. A changed transaction
or material policy revokes approval and requires fresh review. Imported or
recovered copies do not carry signing approval, even when fully signed.

## Finalizing the approved collection

Choose Finalize transaction after every input in the approved collection has
its required verified signatures. The local service rechecks the unsigned
transaction and independent previous-output, payment and change evidence before
assembling the final bytes. It retains the approved original, the collected
PSBT and the finalized transaction separately.

The final view shows the transaction ID, witness transaction ID, byte size,
weight and virtual size from the final serialization. The fee rate is shown as
the exact ratio of the verified fee in satoshis to the final virtual size.
Save final transaction writes those wire bytes to a new local file. View final
transaction opens them for offline inspection. A connection change retires the
approval while allowing the finalized public bytes to be saved or inspected.
Finalization makes no daemon request. Any later submission requires a
separate action with fresh live evidence.

## Checking node policy and broadcasting

With the exact approved collection finalized and its wallet online, use Check
node policy to test those bytes. A rejection reports this node's current
admission decision and retains the final bytes for inspection. A successful
check enables Broadcast for 30 seconds in the same context. Broadcast checks
the bytes and current node identity again, records the attempt in the local
metadata store, then sends once. The fee ceiling is 100 sat/vB and the RPC
burn limit is zero.

The transaction and its signing collection are held steady during submission.
Cancel wait can stop the local wait. After a request may have reached the node,
the outcome can remain uncertain and the local attempt prevents another send.
A node acknowledgment supplies the transaction ID. Confirmation and payment
status require separate observations.

File → Submission Attempts lists recent local records. Open recorded transaction
to inspect the saved public bytes after restart. Check Status asks the current
verified node for matching wallet, mempool and chain observations when supported.
The result includes the check time and expires when the context changes or the
observation gets stale. Missing, unavailable and conflicting answers leave
status Unknown. An already-known reply alone does not establish confirmation.

The raw transaction view does not restore permission to send. To deliberately
repeat an attempt, return to the independently proved PSBT, review and approve
the current collection when needed, and finalize the same bytes. After a fresh
policy check, Rebroadcast same bytes revalidates approval and the recorded
transaction and advances its durable attempt number before sending. Nothing
from the journal is sent automatically.

Choose Monitor transaction for periodic read-only checks on this open record.
Checks normally repeat five seconds after completion and pause after ten
minutes, when you change documents or nodes, or when you choose Stop
monitoring. A known tip change retires the previous current observation as
verification runs again. Going offline shows the last observation with its age.
Reconnecting to the same verified node can resume the check.

An active block can supply confirmations, height and time. A reorg is shown
when the node proves the previously confirmed block is outside its active
chain. A competing spend is shown only with an exact transaction consuming a
shared outpoint and a matching mempool or active-block observation. If an
earlier mempool transaction is no longer found, the reason remains Unknown.

## Editing a transaction

Edit transaction opens local fields for unsigned raw transactions and fixed
unsigned PSBT v0 or v2 documents. Return to editing retires an existing
review and approval before any changes. Apply validates the whole edit and
opens a separate working document. Later edits update that working document
while the original source and its PSBT records stay available in their own
tab. A failed edit leaves both the source and pending fields intact.

The editor changes supported versions (1 through 3 for raw and PSBTv0, or 2 and
3 for the pinned PSBTv2 provider), unsigned 32-bit locktime and sequence
fields, and exact whole-satoshi output values. An input outpoint or
output script can be replaced when its PSBT map has no attached records that
would be detached. Move earlier and Move later, or Ctrl+Up and Ctrl+Down,
move the selected transaction row and its complete PSBT map together.
Signed, finalized, conflicting, or actively negotiated documents remain
read-only. Input-required PSBT locktime cannot be overridden by changing the
fallback. The editor describes height or timestamp constraints without
claiming present maturity.

Header changes and reorder can preserve exact independent payment evidence
within the session when every matched input and output still agrees and
intended change has a unique matched output. A fresh explicit review and
approval are still required. Changed amounts, addresses, inputs or imported
negotiations without independent intent can be exported for inspection.
Prepare a new proved Send draft before seeking approval for those payments.
Public recovery preserves applied original and working bytes. It restores
neither staged fields nor signing approval.
