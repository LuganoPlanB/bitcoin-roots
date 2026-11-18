# Transactional public metadata store

`adapters/storage/MetadataStore` uses the existing pinned SQLite 3.54.1 target.
It accepts an explicit file in an existing private local directory, creates new
files with owner-only access, and rejects final-component symlinks, nonregular
files and nonprivate files/directories on the qualified Unix path. It searches
no daemon home and accesses no Core database. Other native platform private-file
qualification remains unavailable. Application startup reads an existing public
recovery store; opt-in [document recovery](session-recovery.md) creates it when
requested. Explicit [protected sessions](protected-sessions.md) use the native
Linux password provider and retain the SQLite database only in memory.

A wallet scope is a durable backend ID, network genesis and exact daemon-wallet
name, including the unnamed wallet. The ID is independent of transient RPC
sessions. There is no account grouping or descriptor-account ledger. Separate
tables own labels, wallet display names, sort choices, integer column widths,
address/outpoint freezes and public draft editor headers. Freeze preferences
are distinct from daemon locks or temporary draft reservations. Public draft
headers contain only document ID, revision, note and optional exact editor value;
they carry no approval, automatic command or opaque key/PSBT/transaction blob.
The separate session snapshot owns document recovery and exact selected/excluded
inputs; these older headers do not represent executable transaction state.

Workspace preferences are global and restore-documents defaults to false.
Backend profiles store only endpoint/proxy/TLS settings, network/wallet and an
opaque OS secure-store item reference. Cookie authentication profiles cannot
be persisted through this API. No password, cookie value, seed or private-key
column exists. The store does not resolve credentials or implement a keychain.
Free-form user notes/labels are public text, not a private-material import path.
The optional native password provider protects complete metadata at rest and
closes the in-memory store on GUI lock. OS RPC credential-provider integration
remains separate; no keychain or wallet-unlock claim follows from local encryption.

All writes use prepared parameters and explicit transactions. Wallet snapshots,
workspace preferences and profiles compare the caller's expected revision under
an immediate writer transaction. A stale writer receives a conflict, preserving
the previous valid record. Revisions never wrap; SQLite revision counters stop
at signed-int64 exhaustion. Full uint64 draft revisions/lock epochs serialize
as canonical decimal text, while Money uses SQLite int64 bindings and checked
integer reads, including signed extremes and null/unknown values. No financial
value passes through double. Transaction/output validation remains the domain's
responsibility; a stored editor value is not an approved spend.

Version 1 owns user metadata, version 2 adds an independent observation table,
and version 3 adds an atomic, opt-in public recovery record. Version 4 retains
the 16 most recently verified wallet identities in the same metadata store.
A writer merges recency inside an immediate transaction. The list survives
restart and follows the store's encryption and lock. It stores only the public
origin identifier, genesis and exact wallet name. A recent entry supplies no
credentials, connection, daemon mutation or automatic reopening.
Version 5 adds wallet-scoped receive attempts and a pool of up to eight
verified public addresses. The attempt is recorded before an explicit daemon
allocation. Its serial is checked when acknowledging success or clearing an
uncertain attempt. Offline consumption marks one reserved address issued in
the same store transaction before displaying it. Issued records prevent
duplicate reservations. Each scope keeps up to 4,096 allocation records and
blocks further allocations at that bound. These records follow the store's
encryption and lock and carry no replay authority.

Schema creation/migration and version changes commit together. Application ID,
exact known schema, quick integrity check and foreign keys fail closed for
corrupt, foreign or future stores. Failures never recreate/delete an existing
database. The adapter uses rollback journaling with EXTRA synchronization,
defensive/untrusted-schema settings, disabled attachments and bounded SQLite
lengths, page count, busy waits, SQL VM progress budget and result collections.
The progress budget does not impose a filesystem I/O deadline. A
connection belongs to one worker/owner thread; cross-thread calls are rejected.

Observation writes have a separate typed API with no user-metadata fields. They
retain original timestamp, height, source freshness and the full request context;
older timestamps cannot replace newer entries. Reloaded provenance is always
Cached, never Live. The table cannot overwrite labels, freezes, drafts or wallet
revision. Services must still reject obsolete session/revision/epoch results
before saving, as required by the refresh and later lock/session contracts.

`roots_gui.storage.schema` checks reopen persistence, scope isolation, exact
amounts/counters, metadata/cache separation, v1→v2 migration, rollback on an
injected real-VFS journal-sync error, SQLITE_FULL during writes and migration,
corrupt/foreign/future/symlink rejection, and two independent processes racing
revisions and writer locks. Synthetic private scratch and all actors settle.
Power-cut hardware/filesystem behavior is not qualified by these process tests.
The durability policy follows SQLite's [atomic commit documentation](https://www.sqlite.org/atomiccommit.html),
[configuration API](https://www.sqlite.org/c3ref/c_dbconfig_defensive.html),
[pragmas](https://www.sqlite.org/pragma.html) and
[exact integer bindings](https://www.sqlite.org/c3ref/bind_blob.html), checked
against the bundled public header. No library upgrade or installation is needed.

## Wallet rescan records

Schema version 6 stores at most one pending rescan range and a monotonic ticket
per backend, genesis and exact daemon wallet. Admission commits that record
before dispatch. The owner uses the metadata path's private rescan guard to
serialize dispatch and acknowledgment across GUI instances sharing the store.
The record survives restart and protection with the rest of the metadata.
Opening a recovered workspace never dispatches a scan. An unconfirmed result
requires a fresh wallet check and explicit acknowledgment before another review.
