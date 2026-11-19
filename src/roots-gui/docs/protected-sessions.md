# Local workspace protection

Lock current / Lock all and Ctrl+L protect the entire GUI workspace. On the
first use, choose a local password and confirm it. Protect and lock explicitly
enables public recovery and encrypts the local metadata store. Existing public
recovery remains optional until this explicit action. This is neither a daemon
password nor a wallet-unlock request: GUI locking changes no daemon encryption
or unlock timeout, and cannot cancel work already accepted by a daemon. Unlock
never signs or broadcasts. A backend connection must be tested again afterward.

The Linux native provider is installed libsodium ≥1.0.18, discovered through
pkg-config. No package installation, system service or runtime download occurs.
This implementation was exercised with the Debian1.0.18-1+deb13u1 SDK/runtime;
its complete headers, library and ISC-license identities are retained in the
execution evidence. Native keychain integration is unavailable here; the
implemented protection uses an explicit local password. This is not a claim
that the application supplies an OS credential-store provider for RPC profiles.

The wrapper follows libsodium's documented [password-derived key API](https://doc.libsodium.org/password_hashing/default_phf)
and [authenticated file-encryption API](https://doc.libsodium.org/secret-key_cryptography/secretstream).
It pins Argon2id13 with three passes and64MiB, a fresh16-byte salt, and
secretstream XChaCha20-Poly1305 with the library's generated stream header and
FINAL tag. It implements no cipher, nonce generator, MAC or key schedule.
The small versioned prefix records fixed KDF parameters; unknown prefixes fail
before KDF work. Prefix/salt are authenticated additional data. Wrong passwords,
modified/truncated streams and missing final tags expose no plaintext to SQLite.
The maximum serialized database is32MiB. Future parameter changes require a new
explicit format rather than interpreting attacker-selected KDF costs.

Protection covers the complete SQLite database: labels, names, layouts,
freezes, profiles containing secure-item references only, recovery and dated
observations. SQLite runs in memory after authenticated decryption; temp data
stays in memory. Only ciphertext is written to the `.protected` file. A fresh
owner-only ciphertext file is synchronized and atomically renamed, followed by
directory synchronization. Protected writes retain an in-memory pre-write image
and restore its revision/data if persistence fails. A failure after rename may
leave the complete new ciphertext published but durability uncertain; retry
requires reading the file, never replaying a transaction command. Physical
power-cut/filesystem qualification is separate from these process checks.

Plaintext-to-protected conversion takes an exclusive guarded ownership boundary,
serializes the complete valid database, writes and authenticates its protected
copy, then removes the old plaintext pathname. Other current instances prevent
conversion; a protected store has one owner at a time. Plaintext reopening does
not bypass a protected copy. The guard is advisory among cooperating instances,
not a defense against a process already running as the same user. Removing a
plaintext pathname cannot erase earlier disk blocks, backups or snapshots.

If conversion stops after encrypted publication but before plaintext removal,
restart still requires the protection password and exclusive workspace ownership.
After authentication, matching complete copies finish conversion safely. If the
copies differ, both remain untouched and the lock panel requires a deliberate
choice. “Keep protected workspace; encrypt other copy” keeps the authenticated
protected workspace and saves the other complete file as
`metadata.sqlite.conversion-backup.protected`, encrypted with the same password.
It authenticates and compares that backup before removing the plaintext path.
An interrupted resolution can retry using the matching backup; a different or
damaged existing backup is never overwritten. Wrong passwords, damaged protected
data and unresolved conflicts expose no documents and retain the original bytes.
The backup has the normal protected-store format: retain it with the password;
for separate recovery, copy it to an isolated private data directory as
`roots-gui/metadata.sqlite.protected` and select that directory with XDG_DATA_HOME
when starting the application. Do not replace an active workspace. Unlock and
public-document restoration remain separate deliberate actions; neither replays
RPC or transaction commands.

Privacy locking saves opted-in work, seals current views into authenticated
memory ciphertext, advances an epoch and requests cancellation. It removes
documents, edit buffers, navigation, dialogs, copied previews and accessibility
content, closes SQLite and releases the derived key. Known owned buffers are
cleared; ImGui, SQLite, allocator and operating-system copies prevent a complete
RAM-erasure claim. The derived key uses libsodium guarded/locked allocation and
is released on lock. No plaintext password is retained in metadata, logs,
semantic values or capture artifacts. Password reveal is deliberate and resets
after an attempt. Copy and Cut cannot publish protection passwords to the
system clipboard, including while the text is revealed. Failed unlocks keep
the workspace concealed.

The owner-thread gate stamps signing, address-load, label-save and refresh
operations with the full backend/network/wallet/document/revision/lock epoch.
Only a matching live stamp may adopt a result. Lock cancels its tokens; unlock
creates a fresh cancellation source without making old tickets valid. Actual
connection tests and refresh workers also fence/cancel on lock, discard late
completion, remove cached observations and suppress transport scheduling. Tests
for future signing/address/label consumers are typed held-boundary fixtures,
not evidence that live signing or those future services have shipped.

An in-memory opt-out draft survives a lock/unlock cycle without being persisted.
Restart of a protected store requires its password and then the existing
deliberate recovery choice. If another instance changed recovery while locked,
the original locked views cannot silently overwrite it: choosing the latest
saved workspace is explicit. Recovery and Clear/close still issue no RPC and
restore no capability, approval or executable command.

Linux is the implemented provider. macOS/Windows native private-file/keychain
and lock qualification remain explicit follow-ons. Native virtual-display
checks do not establish physical hardware or operating-system memory protection.
