#!/usr/bin/env bash
# Materialize the technically accepted Roots 29.4 commit topology in disposable state.

set -Eeuo pipefail
IFS=$'\n\t'

readonly CORE_COMMIT="3fc0865963a38b871e9f7d94e6151c4953563516"
readonly CORE_TREE="38ad59b187f59647eb90ad1347bc481485ef4d01"
readonly CORE_TAG_OBJECT="4e70eab99b60f7718b78e2158de9fb82726f3cec"
readonly CANDIDATE_COMMIT="f771e13259f23f02efc215327f2806d421cc7339"
readonly CANDIDATE_TREE="39a5e30207a09962e78ae81c24cc65b1e478ef90"
readonly CANDIDATE_REF="refs/remotes/origin/ci/l7-validation/39a5e302"
readonly EXPECTED_TARGET_COMMIT="cbc88cff9b35b95a549c0313e424e13093fcd6a1"

die()
{
    printf 'canonical-lineage: %s\n' "$*" >&2
    exit 1
}

[[ $# -eq 2 ]] || die "usage: $0 SOURCE_REPOSITORY NEW_DESTINATION"
for command in git jq realpath sha256sum
do
    command -v "$command" >/dev/null 2>&1 || die "$command is required"
done

source_repository="$(realpath "$1")"
readonly source_repository
readonly destination="$2"
script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly script_directory
readonly manifest="$script_directory/../adaptation-manifest-29.3.json"
readonly tag_payload="$script_directory/materials/core-v29.4.tag"

[[ -d "$source_repository/.git" ]] || die "source is not a Git worktree"
[[ -f "$manifest" ]] || die "adaptation manifest is unavailable"
[[ -f "$tag_payload" ]] || die "Core tag payload is unavailable"
[[ ! -e "$destination" ]] || die "destination already exists"

git init -q "$destination"
git -C "$destination" fetch -q --no-tags "$source_repository" "$CANDIDATE_REF"

actual_core_tree="$(git -C "$destination" rev-parse "$CORE_COMMIT^{tree}")"
[[ "$actual_core_tree" == "$CORE_TREE" ]] || die "Core tree does not match its lock"
actual_candidate_tree="$(git -C "$destination" rev-parse "$CANDIDATE_COMMIT^{tree}")"
[[ "$actual_candidate_tree" == "$CANDIDATE_TREE" ]] || die "candidate tree does not match its lock"
actual_candidate_parent="$(git -C "$destination" rev-parse "$CANDIDATE_COMMIT^")"
[[ "$actual_candidate_parent" == "$CORE_COMMIT" ]] || die "validation candidate has the wrong Core parent"

actual_tag="$(git -C "$destination" hash-object -t tag -w "$tag_payload")"
[[ "$actual_tag" == "$CORE_TAG_OBJECT" ]] || die "Core annotated tag object does not match its lock"
git -C "$destination" update-ref refs/tags/v29.4 "$actual_tag"
[[ "$(git -C "$destination" rev-parse refs/tags/v29.4^{})" == "$CORE_COMMIT" ]] ||
    die "Core annotated tag peels to the wrong commit"

git -C "$destination" read-tree "$CORE_TREE"
parent="$CORE_COMMIT"
unit_count="$(jq '.units | length' "$manifest")"
readonly unit_count
[[ "$unit_count" -eq 16 ]] || die "canonical manifest must contain sixteen units"

for ((index = 0; index < unit_count; ++index))
do
    unit_id="$(jq -r ".units[$index].id" "$manifest")"
    [[ "$unit_id" =~ ^[a-z0-9][a-z0-9-]*$ ]] || die "invalid unit ID"
    mapfile -d '' -t paths < <(jq -j ".units[$index].touched.paths[] | ., \"\\u0000\"" "$manifest")
    [[ ${#paths[@]} -gt 0 ]] || die "unit has no declared paths: $unit_id"
    for path in "${paths[@]}"
    do
        entry="$(git -C "$destination" ls-tree \
            --format='%(objectmode) %(objecttype) %(objectname)' \
            "$CANDIDATE_TREE" -- "$path")"
        if [[ -z "$entry" ]]
        then
            git -C "$destination" update-index --force-remove -- "$path"
            continue
        fi
        IFS=' ' read -r mode object_type blob _ <<< "$entry"
        [[ "$object_type" == "blob" ]] || die "candidate entry is not a blob: $path"
        git -C "$destination" update-index --add --cacheinfo "$mode,$blob,$path"
    done
    tree="$(git -C "$destination" write-tree)"
    [[ "$tree" != "$(git -C "$destination" rev-parse "$parent^{tree}")" ]] ||
        die "unit produces an empty canonical commit: $unit_id"
    timestamp=$((1783508300 + index))
    commit="$({
        printf 'replay: %s\n' "$unit_id"
    } | env \
        GIT_AUTHOR_NAME="Bitcoin Roots Replay" \
        GIT_AUTHOR_EMAIL="replay@bitcoinroots.invalid" \
        GIT_AUTHOR_DATE="@$timestamp +0000" \
        GIT_COMMITTER_NAME="Bitcoin Roots Replay" \
        GIT_COMMITTER_EMAIL="replay@bitcoinroots.invalid" \
        GIT_COMMITTER_DATE="@$timestamp +0000" \
        git -C "$destination" commit-tree "$tree" -p "$parent")"
    printf 'commit=%s unit=%s tree=%s\n' "$commit" "$unit_id" "$tree"
    parent="$commit"
done

readonly target_commit="$parent"
[[ "$target_commit" == "$EXPECTED_TARGET_COMMIT" ]] || die "canonical target commit mismatch"
target_tree="$(git -C "$destination" rev-parse "$target_commit^{tree}")"
readonly target_tree
[[ "$target_tree" == "$CANDIDATE_TREE" ]] || die "canonical series does not reproduce the accepted candidate tree"
git -C "$destination" update-ref refs/heads/roots-29.4-canonical "$target_commit"
git -C "$destination" checkout -q --detach "$target_commit"

printf 'base_commit=%s\n' "$CORE_COMMIT"
printf 'target_commit=%s\n' "$target_commit"
printf 'target_tree=%s\n' "$target_tree"
