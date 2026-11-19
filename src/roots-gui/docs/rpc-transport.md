# Authenticated transport boundary

`adapters/rpc/Transport::Call` is the production worker operation backed by the
vendored curl 8.23.0 target. It performs one JSON-RPC 2.0 POST. It starts no
daemon, accesses no daemon state and imports no Core headers or libraries.
It returns an owned value; the caller must deliver it on the UI owner thread
with the immutable request context and reject obsolete identities/lock epochs.
The [connection service](server-connection.md) supplies capability probing and
profile Test/Apply. The [refresh service](rpc-refresh.md) bounds repeated
observations; normal startup still performs no RPC call.

`services/connection.h` is the shared Server form validation boundary. A profile
contains an explicit host/port, TLS trust choice, proxy and credential reference.
It contains no password or URL userinfo, path, query or fragment. Wallet names
are separately encoded as one UTF-8 URL path component, including literal slash,
percent, question mark and non-ASCII characters. Direct connections ignore
environment proxy variables. SOCKS5 resolves names at the proxy (including
onion names); HTTP proxies use CONNECT so origin authentication stays inside
the tunnel. Proxy authentication is outside this boundary and is unsupported.
HTTP Basic authenticates the intended endpoint. Plain HTTP is unencrypted;
remote profiles should use verified HTTPS or a deliberately selected trusted
tunnel. TLS peer and hostname verification cannot be disabled. An explicit CA
file may replace the platform trust bundle. Redirects are errors at every 3xx
status and never contact their target. Netrc, cookie jars and debug logging are
disabled/unconfigured. A configured `SSLKEYLOGFILE` fails closed before curl
initialization to prevent exporting TLS session secrets. New handles do not
retain credentials or connections.

`CredentialSource` resolves credentials afresh for every call. Its secure-store
implementation must bind the opaque item ID to the supplied origin. The
transport never persists the resolved value. `CookieCredentials` reads only
the explicitly selected absolute file, with a 4096-byte bound and regular-file
check; it never searches a user's node home. On Linux it opens nonblocking with
close-on-exec and checks the opened descriptor. An atomically replaced cookie
is read on the next explicit call. A 401 is returned without replay; there is
no automatic cookie-renewal retry. User/password authentication uses the same
credential-source port. The current test suite uses an origin-bound secure-store
double; native keychain integration belongs to the protected-store work package.
Credential buffers are cleared on destruction where owned, without promising
complete erasure of allocator, library or OS copies.

The dedicated daemon wallet unlock sends a bounded passphrase only to the exact
wallet route over verified TLS or direct numeric loopback. The transport builds
its request body directly in a buffer that is cleared on exit and returns only
a typed error or the empty success result. Unlock responses and credentials
never enter diagnostic text. The daemon owns the unlock timeout.

Connect and total deadlines, request/body/header sizes, JSON nesting and node
counts have enforced hard maxima. The multi loop checks cancellation at most
50ms between polls, with asynchronous DNS required at runtime. Callers still
must bound concurrent jobs and queue depth. Cancellation or an ambiguous error
after dispatch sets `mutation_uncertain` for state-changing requests, including
RPC errors that might follow partial side effects. This is
not proof that a daemon stopped processing them. No transport call is retried,
including authentication, disconnect, timeout, redirect or Expect errors.
Callers reconcile an uncertain operation before offering a deliberate retry.

JSON validates UTF-8, escapes/surrogate pairs, full input consumption, duplicate
members, grammar, depth and node counts. Numeric lexemes remain strings; exact
amount consumers use `Money::ParseJsonBtc`, and request decimals are sent intact.
Reply IDs must match the original integer token and JSON-RPC version. Structured
RPC error codes remain available; arbitrary error messages, bodies, addresses,
URLs and credentials never become diagnostics. `ErrorName` supplies fixed
secret-free diagnostics only. Successful result trees are sensitive caller data
and must obey the existing privacy/lock rules; they are never operational logs.

`roots_gui.rpc.json` checks JSON and profile validation.
`roots_gui.rpc.transport` drives the actual C++ adapter against owned loopback
HTTP, TLS and SOCKS/CONNECT fixtures. It includes request identity, exact values,
cookie rotation, redirect non-delivery, malformed/truncated/limited responses,
timeout, cancellation, redaction, verified TLS and proxy DNS failure. Direct
external DNS, native secure stores, remote networks and Windows/macOS runtime
qualification remain unexecuted here. No real credentials or funds are used.
