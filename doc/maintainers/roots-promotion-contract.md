# Roots 29.4 promotion contract

The control branch and production history are deliberately separate. The control
branch `codex/roots-29-4-release` contains validators and workflow code; it is
not the source of a Roots 29.4 release. Production starts at the annotated Core
`v29.4` commit, contains only the reviewed Roots adaptations, then later moves
by exact fast-forward from `integration/roots-29.4` to `roots/29.4`.

`contrib/roots/promotion-29.4.json` is the fail-closed machine-readable lock.
It records Core's tag object, peeled commit and tree, the replay candidate tree,
canonical commit, forbidden old-trunk ancestor, production refs/tag, and an
explicitly false authorization bit. Candidate data may enter trusted validation
only as the recorded commit or an exact bundle carrying it. No branch name,
working tree, vendor snapshot, submodule, or mutable remote ref is an input.

Run the non-mutating validator twice from a neutral environment:

```bash
env -i PATH="$PATH" LC_ALL=C LANG=C TZ=UTC \
  python3 contrib/devtools/roots-promotion-contract.py \
  contrib/roots/promotion-29.4.json --repository /path/to/production-clone
```

It rejects shallow repositories, alternates, replacement refs, host Git object
overrides, moved tags/heads, non-Core parents, merge history, old-trunk ancestry,
and submodule or vendor imports. Passing it authorizes neither a ref update nor
a tag: authorization remains false until later independent review.
