#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 3 ]]; then
    printf 'Usage: %s RELEASE_TAG RELEASE_COMMIT EVENT_NAME\n' "$0" >&2
    exit 2
fi

release_tag=$1
release_commit=$2
event_name=$3
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

"$script_dir/validate-release-tag.sh" "$release_tag"

core_version=${release_tag#v}
core_version=${core_version%%-roots.*}
canonical_ref="refs/remotes/origin/roots/${core_version}"
core_ref="refs/tags/v${core_version}"
core_remote=${RELEASE_CORE_REMOTE:-https://github.com/bitcoin/bitcoin.git}

git fetch --no-tags origin \
    "refs/heads/roots/${core_version}:${canonical_ref}"
git fetch --no-tags "$core_remote" "${core_ref}:${core_ref}"

case "$event_name" in
    workflow_dispatch)
        if [[ ! "$release_commit" =~ ^[0-9a-f]{40}$ ]]; then
            printf 'Manual release rehearsal requires a full lowercase 40-hex commit\n' >&2
            exit 1
        fi
        head_commit="$(git rev-parse --verify 'HEAD^{commit}')"
        if [[ "$head_commit" != "$release_commit" ]]; then
            printf 'Checked-out commit does not match requested rehearsal commit\n' >&2
            exit 1
        fi

        set +e
        remote_tag="$(git ls-remote --exit-code --tags origin \
            "refs/tags/${release_tag}" "refs/tags/${release_tag}^{}" 2>/dev/null)"
        remote_status=$?
        set -e
        if [[ "$remote_status" -eq 0 ]]; then
            printf 'Release rehearsal requires the remote tag to be absent: %s\n' "$release_tag" >&2
            exit 1
        fi
        if [[ "$remote_status" -ne 2 ]]; then
            printf 'Unable to establish remote tag absence for %s\n' "$release_tag" >&2
            exit 1
        fi
        if [[ -n "$remote_tag" ]]; then
            printf 'Unexpected remote tag response for %s\n' "$release_tag" >&2
            exit 1
        fi
        if git show-ref --verify --quiet "refs/tags/${release_tag}"; then
            printf 'Release rehearsal refuses an existing local tag: %s\n' "$release_tag" >&2
            exit 1
        fi

        GIT_COMMITTER_NAME='Bitcoin Roots release rehearsal' \
        GIT_COMMITTER_EMAIL='release-rehearsal@invalid' \
        git tag --annotate "$release_tag" "$release_commit" \
            --message="Ephemeral rehearsal for ${release_tag}"
        ;;
    push)
        if [[ -n "$release_commit" ]]; then
            printf 'Tag-triggered releases do not accept a manual release commit\n' >&2
            exit 1
        fi
        git fetch --no-tags origin \
            "refs/tags/${release_tag}:refs/tags/${release_tag}"
        ;;
    *)
        printf 'Unsupported release event: %s\n' "$event_name" >&2
        exit 1
        ;;
esac

RELEASE_CANONICAL_REF="$canonical_ref" RELEASE_CORE_REF="$core_ref" \
    "$script_dir/validate-release-source.sh" "$release_tag"
