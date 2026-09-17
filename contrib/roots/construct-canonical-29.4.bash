#!/usr/bin/env bash
# Construct the accepted 29.4 Roots lineage in a fresh, private repository.

set -Eeuo pipefail
IFS=$'\n\t'
umask 077

readonly CORE_COMMIT=3fc0865963a38b871e9f7d94e6151c4953563516
readonly CORE_TREE=38ad59b187f59647eb90ad1347bc481485ef4d01
readonly CORE_TAG_OBJECT=4e70eab99b60f7718b78e2158de9fb82726f3cec
readonly CANDIDATE_TREE=39a5e30207a09962e78ae81c24cc65b1e478ef90
readonly CANONICAL_COMMIT=cbc88cff9b35b95a549c0313e424e13093fcd6a1
readonly OUTPUT_REF=refs/roots/private/canonical-29.4

die() { printf 'construct-canonical-29.4: %s\n' "$*" >&2; exit 1; }

[[ $# -eq 2 ]] || die "usage: $0 VERIFIED_SOURCE_REPOSITORY NEW_DESTINATION"
for command in git jq realpath sha256sum; do command -v "$command" >/dev/null || die "$command is required"; done

source_repository=$(realpath "$1")
destination=$2
script_directory=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
manifest="$script_directory/adaptation-manifest-29.3.json"
proposal="$script_directory/replay-29.4-proposal"
acceptance="$proposal/acceptance-evidence.json"
materials="$proposal/replay-materials.json"
promotion="$script_directory/promotion-29.4.json"
tag_payload="$proposal/materials/core-v29.4.tag"
created_destination=0

cleanup() {
    local status=$?
    if (( status != 0 && created_destination )); then rm -rf -- "$destination"; fi
    return "$status"
}
trap cleanup EXIT
trap 'exit 130' INT HUP TERM

[[ -d "$source_repository/.git" ]] || die "source is not a Git worktree"
[[ ! -e "$destination" ]] || die "destination already exists"
for required in "$manifest" "$acceptance" "$materials" "$promotion" "$tag_payload"; do
    [[ -f "$required" && ! -L "$required" ]] || die "required input is unavailable or symlinked"
done
lineage_row="$proposal/lineage-row-29.4.json"
[[ -f "$lineage_row" && ! -L "$lineage_row" ]] || die "lineage record is unavailable or symlinked"
[[ ! -e "$source_repository/.git/objects/info/alternates" ]] || die "source uses object alternates"
[[ ! -e "$source_repository/.git/info/grafts" ]] || die "source uses grafts"
[[ -z $(git -C "$source_repository" status --porcelain) ]] || die "source worktree is dirty"
[[ -z $(git -C "$source_repository" for-each-ref refs/replace) ]] || die "source has replacement refs"

# Reject malformed or substituted control inputs before reserving a destination
# or importing any Git objects.
jq -e --arg core "sha1:$CORE_COMMIT" --arg tree "sha1:$CORE_TREE" --arg candidate "sha1:$CANDIDATE_TREE" '
    .status == "accepted" and .candidate.base_commit == $core and .candidate.base_tree == $tree and .candidate.tree == $candidate and .reproducibility.status == "pass"' "$acceptance" >/dev/null || die "accepted replay state is not locked"
expected_materials_digest=$(jq -r '.frozen_inputs.replay_materials' "$lineage_row")
[[ "$expected_materials_digest" == "sha256:$(sha256sum "$materials" | awk '{print $1}')" ]] || die "replay materials digest does not match accepted lineage"
expected_manifest_digest=$(jq -r '.frozen_inputs.adaptation_manifest_29_3' "$lineage_row")
[[ "$expected_manifest_digest" == "sha256:$(sha256sum "$manifest" | awk '{print $1}')" ]] || die "adaptation manifest digest does not match accepted lineage"

# The source is a local, pre-verified object store. This command never resolves
# a remote name or network URL and imports only the two locked objects.
[[ "$source_repository" != *://* ]] || die "source must be a local repository"
mkdir -- "$destination" || die "cannot reserve destination"
created_destination=1
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_NO_REPLACE_OBJECTS=1 GIT_OPTIONAL_LOCKS=0 LC_ALL=C TZ=UTC
git -C "$destination" init -q --initial-branch=private-construction
git -C "$destination" -c core.hooksPath=/dev/null -c commit.gpgSign=false \
    -c tag.gpgSign=false -c rerere.enabled=false -c core.useReplaceRefs=false \
    fetch -q --no-tags "$source_repository" "$CORE_COMMIT" "$CANONICAL_COMMIT"

core_tree=$(git -C "$destination" rev-parse "$CORE_COMMIT^{tree}")
[[ "$core_tree" == "$CORE_TREE" ]] || die "Core tree does not match lock"
candidate_tree=$(git -C "$destination" rev-parse "$CANONICAL_COMMIT^{tree}")
[[ "$candidate_tree" == "$CANDIDATE_TREE" ]] || die "accepted replay result tree does not match lock"
[[ $(git -C "$destination" rev-list --first-parent --count "$CANONICAL_COMMIT" "^$CORE_COMMIT") == 16 ]] || die "accepted lineage does not contain sixteen commits"
first_lineage_commit=$(git -C "$destination" rev-list --first-parent "$CANONICAL_COMMIT" -n 16 | tail -n 1)
[[ $(git -C "$destination" rev-parse "$first_lineage_commit^") == "$CORE_COMMIT" ]] || die "accepted lineage is not Core-rooted"

jq -e --arg core "sha1:$CORE_COMMIT" --arg tree "sha1:$CORE_TREE" \
    --arg candidate "sha1:$CANDIDATE_TREE" --arg canonical "sha1:$CANONICAL_COMMIT" '
    .status == "accepted" and .candidate.base_commit == $core and
    .candidate.base_tree == $tree and .candidate.tree == $candidate and
    .reproducibility.status == "pass" and
    (.gates | all(.[]; (.critical | not) or (.status == "pass" and .waiver == false)))' \
    "$acceptance" >/dev/null || die "accepted replay state is not locked"
jq -e --arg core "sha1:$CORE_COMMIT" --arg tree "sha1:$CORE_TREE" \
    --arg candidate "sha1:$CANDIDATE_TREE" --arg canonical "sha1:$CANONICAL_COMMIT" '
    .core.commit == $core and .core.tree == $tree and .candidate.tree == $candidate and
    .candidate.canonical_commit == $canonical and .candidate.input_commit == $canonical and
    .production.head == $canonical and .authorization == false' "$promotion" >/dev/null ||
    die "promotion record does not lock this construction"
expected_materials_digest=$(jq -r '.frozen_inputs.replay_materials' "$lineage_row")
actual_materials_digest="sha256:$(sha256sum "$materials" | awk '{print $1}')"
[[ "$expected_materials_digest" == "$actual_materials_digest" ]] || die "replay materials digest does not match accepted lineage"
expected_manifest_digest=$(jq -r '.frozen_inputs.adaptation_manifest_29_3' "$lineage_row")
actual_manifest_digest="sha256:$(sha256sum "$manifest" | awk '{print $1}')"
[[ "$expected_manifest_digest" == "$actual_manifest_digest" ]] || die "adaptation manifest digest does not match accepted lineage"
jq -e '
    .schema_version == 1 and (.materials | type == "object" and length == 5) and
    (.materials | to_entries | all(.[]; (.key | type == "string" and startswith("roots:l7-")) and
        (.value.mechanism == "patch" or .value.mechanism == "module/data") and
        ((.value.patch // .value.source) | (type == "string" and test("^[a-zA-Z0-9._/-]+$") and (contains("..") | not))) and
        (.value.expected_after_tree | type == "string" and test("^[0-9a-f]{40}$")) and
        (.value.expected_before_tree | type == "string" and test("^[0-9a-f]{40}$"))))' \
    "$materials" >/dev/null || die "replay materials contract is invalid"
while IFS= read -r material_path; do
    [[ -f "$proposal/$material_path" && ! -L "$proposal/$material_path" ]] || die "replay material is unavailable or symlinked"
done < <(jq -r '.materials[] | (.patch // .source)' "$materials")

actual_tag=$(git -C "$destination" hash-object -t tag -w "$tag_payload")
[[ "$actual_tag" == "$CORE_TAG_OBJECT" ]] || die "Core tag payload does not match lock"
git -C "$destination" update-ref refs/tags/v29.4 "$actual_tag"
[[ $(git -C "$destination" rev-parse refs/tags/v29.4^{}) == "$CORE_COMMIT" ]] || die "Core tag peels incorrectly"

unit_count=$(jq '.units | length' "$manifest")
[[ "$unit_count" -eq 16 ]] || die "manifest must have exactly sixteen units"
[[ $(jq '[.units[].id] | length == (unique | length) and all(.[]; type == "string" and length > 0)' "$manifest") == true ]] ||
    die "manifest has duplicate or empty unit IDs"
git -C "$destination" read-tree "$CORE_TREE"
parent=$CORE_COMMIT
for ((index = 0; index < unit_count; ++index)); do
    unit_id=$(jq -r ".units[$index].id" "$manifest")
    mapfile -d '' -t paths < <(jq -j ".units[$index].touched.paths[] | ., \"\\u0000\"" "$manifest")
    [[ ${#paths[@]} -gt 0 ]] || die "unit has no declared paths: $unit_id"
    for path in "${paths[@]}"; do
        [[ "$path" != /* && "$path" != *'..'* ]] || die "unsafe manifest path"
        entry=$(git -C "$destination" ls-tree --format='%(objectmode) %(objecttype) %(objectname)' "$CANDIDATE_TREE" -- "$path")
        if [[ -z "$entry" ]]; then git -C "$destination" update-index --force-remove -- "$path"; continue; fi
        IFS=' ' read -r mode object_type blob _ <<< "$entry"
        [[ "$object_type" == blob ]] || die "candidate entry is not a blob: $path"
        git -C "$destination" update-index --add --cacheinfo "$mode,$blob,$path"
    done
    tree=$(git -C "$destination" write-tree)
    [[ "$tree" != $(git -C "$destination" rev-parse "$parent^{tree}") ]] || die "unit produces an empty commit: $unit_id"
    timestamp=$((1783508300 + index))
    commit=$({ printf 'replay: %s\n' "$unit_id"; } | env \
        GIT_AUTHOR_NAME='Bitcoin Roots Replay' GIT_AUTHOR_EMAIL='replay@bitcoinroots.invalid' GIT_AUTHOR_DATE="@$timestamp +0000" \
        GIT_COMMITTER_NAME='Bitcoin Roots Replay' GIT_COMMITTER_EMAIL='replay@bitcoinroots.invalid' GIT_COMMITTER_DATE="@$timestamp +0000" \
        git -C "$destination" -c core.hooksPath=/dev/null -c commit.gpgSign=false commit-tree "$tree" -p "$parent")
    parent=$commit
done
[[ "$parent" == "$CANONICAL_COMMIT" ]] || die "canonical commit mismatch"
[[ $(git -C "$destination" rev-parse "$parent^{tree}") == "$CANDIDATE_TREE" ]] || die "canonical tree mismatch"
git -C "$destination" update-ref "$OUTPUT_REF" "$parent"
git -C "$destination" checkout -q --detach "$parent"
printf 'base_commit=%s\ntarget_commit=%s\ntarget_tree=%s\noutput_ref=%s\n' "$CORE_COMMIT" "$parent" "$CANDIDATE_TREE" "$OUTPUT_REF"
