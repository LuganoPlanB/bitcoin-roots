#!/usr/bin/env bash
# Construct the deliberately unreviewed incremental comparison oracle.

set -Eeuo pipefail
IFS=$'\n\t'

readonly ROOTS_29_3_COMMIT="42098b53c57fb6818736c7b6732fa84ffe6ad391"
readonly ROOTS_29_3_TREE="a5708dcbf1d2611360fab68fc6a8e504db1ba95d"
readonly CORE_29_3_COMMIT="99003bed87333f1be51bf3070235591b3a72f007"
readonly CORE_29_3_TREE="d7910bd5e9335128932f1f848a767d773895c4a4"
readonly CORE_29_4_COMMIT="3fc0865963a38b871e9f7d94e6151c4953563516"
readonly CORE_29_4_TREE="38ad59b187f59647eb90ad1347bc481485ef4d01"
readonly CANDIDATE_REF="refs/remotes/origin/ci/l7-validation/39a5e302"
readonly EXPECTED_TREE="c676e8944470cc74fcc213e7368aed359ad8ae55"
readonly EXPECTED_COMMIT="3e29908f7a0131a71309e80a78fe865ec8a50f76"

readonly -a EXPECTED_CONFLICTS=(
    ".github/actions/configure-docker/action.yml"
    ".github/workflows/ci.yml"
    "ci/README.md"
    "doc/man/bitcoin-cli.1"
    "doc/man/bitcoin-qt.1"
    "doc/man/bitcoin-tx.1"
    "doc/man/bitcoin-util.1"
    "doc/man/bitcoin-wallet.1"
    "doc/man/bitcoind.1"
    "src/node/miner.h"
    "src/txmempool.h"
    "src/txrequest.cpp"
)

die()
{
    printf 'incremental-oracle: %s\n' "$*" >&2
    exit 1
}

[[ $# -eq 2 ]] || die "usage: $0 SOURCE_REPOSITORY NEW_DESTINATION"
command -v git >/dev/null 2>&1 || die "git is required"
command -v realpath >/dev/null 2>&1 || die "realpath is required"

source_repository="$(realpath "$1")"
readonly source_repository
readonly destination="$2"
[[ "$(git -C "$source_repository" rev-parse --is-inside-work-tree 2>/dev/null || true)" == "true" ]] || die "source is not a Git worktree"
[[ ! -e "$destination" ]] || die "destination already exists"

git init -q "$destination"
git -C "$destination" fetch -q --no-tags "$source_repository" HEAD
git -C "$destination" fetch -q --no-tags "$source_repository" "$CANDIDATE_REF"

for identity in \
    "$ROOTS_29_3_COMMIT:$ROOTS_29_3_TREE" \
    "$CORE_29_3_COMMIT:$CORE_29_3_TREE" \
    "$CORE_29_4_COMMIT:$CORE_29_4_TREE"
do
    commit="${identity%%:*}"
    expected_tree="${identity#*:}"
    actual_tree="$(git -C "$destination" rev-parse "$commit^{tree}")"
    [[ "$actual_tree" == "$expected_tree" ]] || die "locked input tree mismatch for $commit"
done

git -C "$destination" checkout -q --detach "$ROOTS_29_3_COMMIT"
set +e
git -C "$destination" diff --binary "$CORE_29_3_COMMIT" "$CORE_29_4_COMMIT" |
    git -C "$destination" apply --3way --index >/dev/null 2>&1
readonly apply_status=$?
set -e
[[ $apply_status -ne 0 ]] || die "expected the predeclared manual boundaries"

mapfile -t actual_conflicts < <(
    git -C "$destination" diff --name-only --diff-filter=U | LC_ALL=C sort
)
[[ "${actual_conflicts[*]}" == "${EXPECTED_CONFLICTS[*]}" ]] ||
    die "conflict set differs from the locked oracle"

for path in "${actual_conflicts[@]}"
do
    incoming_blob="$(git -C "$destination" rev-parse ":3:$path")"
    core_blob="$(git -C "$destination" rev-parse "$CORE_29_4_COMMIT:$path")"
    [[ "$incoming_blob" == "$core_blob" ]] || die "incoming blob is not Core 29.4: $path"
    git -C "$destination" checkout -q --theirs -- "$path"
    git -C "$destination" add -- "$path"
done

[[ -z "$(git -C "$destination" diff --name-only --diff-filter=U)" ]] ||
    die "unresolved paths remain"
actual_tree="$(git -C "$destination" write-tree)"
readonly actual_tree
[[ "$actual_tree" == "$EXPECTED_TREE" ]] || die "incremental oracle tree mismatch"
oracle_commit="$({
    printf '%s\n' "oracle: increment Roots 29.3 with Core 29.4 delta"
} | env \
    GIT_AUTHOR_NAME="L7 Incremental Oracle" \
    GIT_AUTHOR_EMAIL="l7-oracle.invalid" \
    GIT_AUTHOR_DATE="2026-09-15T00:00:00Z" \
    GIT_COMMITTER_NAME="L7 Incremental Oracle" \
    GIT_COMMITTER_EMAIL="l7-oracle.invalid" \
    GIT_COMMITTER_DATE="2026-09-15T00:00:00Z" \
    git -C "$destination" commit-tree "$actual_tree" -p "$ROOTS_29_3_COMMIT")"
readonly oracle_commit
[[ "$oracle_commit" == "$EXPECTED_COMMIT" ]] || die "incremental oracle commit mismatch"

printf 'roots_29_3_commit=%s\n' "$ROOTS_29_3_COMMIT"
printf 'core_29_3_commit=%s\n' "$CORE_29_3_COMMIT"
printf 'core_29_4_commit=%s\n' "$CORE_29_4_COMMIT"
printf 'conflict_count=%s\n' "${#actual_conflicts[@]}"
printf 'oracle_tree=%s\n' "$actual_tree"
printf 'oracle_commit=%s\n' "$oracle_commit"
