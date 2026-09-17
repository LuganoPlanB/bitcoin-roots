#!/usr/bin/env bash
set -Eeuo pipefail
readonly TARGET=cbc88cff9b35b95a549c0313e424e13093fcd6a1
readonly TREE=39a5e30207a09962e78ae81c24cc65b1e478ef90
readonly REF=refs/heads/integration/roots-29.4
[[ $# -eq 2 && $2 == 'CREATE-LOCAL-ROOTS-29.4' ]] || { echo 'literal confirmation required' >&2; exit 1; }
repo=$1
[[ -d $repo/.git && ! -f $repo/.git ]] || { echo 'linked worktree rejected' >&2; exit 1; }
[[ -z $(git -C "$repo" status --porcelain) ]] || { echo 'dirty tracked state' >&2; exit 1; }
[[ $(git -C "$repo" rev-parse HEAD) == "$TARGET" ]] || { echo 'wrong branch/head' >&2; exit 1; }
[[ -z $(git -C "$repo" for-each-ref "$REF") ]] || { echo 'existing target' >&2; exit 1; }
[[ $(git -C "$repo" rev-parse "$TARGET^{tree}") == "$TREE" ]] || { echo 'wrong canonical tree' >&2; exit 1; }
git -C "$repo" update-ref "$REF" "$TARGET" "$(printf '0%.0s' {1..40})" || { echo 'existing target or concurrent creation rejected' >&2; exit 1; }
printf 'confirmation=CREATE-LOCAL-ROOTS-29.4\nref=%s\ncommit=%s\ntree=%s\n' "$REF" "$TARGET" "$TREE"
