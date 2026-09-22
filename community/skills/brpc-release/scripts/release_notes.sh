#!/usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Draft Release Notes from GitHub pull requests represented by commits between
# two refs. One merged PR produces at most one entry, even when it has many
# commits. The script fetches the PR title and author through `gh api`.
#
# Usage:
#   release_notes.sh <from-ref> <to-ref> [--repo <owner/repo>]
#
# Example: <to-ref> is the local release tag, which need not be pushed to GitHub.
#   release_notes.sh "$PREV_VERSION" "$VERSION" > "$HOME/brpc_release/$VERSION/notes-draft.md"
#
# `gh auth login` must be completed before running this script. Buckets are
# title-keyword guesses only; review every PR and reword it for users.

set -euo pipefail

FROM=""
TO=""
REPO=apache/brpc

while (( $# )); do
    case $1 in
        --repo)
            [[ $# -ge 2 ]] || { echo "error: --repo requires <owner/repo>" >&2; exit 2; }
            REPO=$2
            shift 2
            ;;
        -h|--help)
            sed -n '20,31p' "$0"
            exit 0
            ;;
        -*)
            echo "error: unknown option '$1'" >&2
            exit 2
            ;;
        *)
            if [[ -z ${FROM} ]]; then
                FROM=$1
            elif [[ -z ${TO} ]]; then
                TO=$1
            else
                echo "error: too many positional arguments" >&2
                exit 2
            fi
            shift
            ;;
    esac
done

if [[ -z ${FROM} || -z ${TO} ]]; then
    echo "usage: $(basename "$0") <from-ref> <to-ref> [--repo <owner/repo>]" >&2
    exit 2
fi
if ! [[ ${REPO} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
    echo "error: '${REPO}' is not a valid GitHub owner/repository" >&2
    exit 2
fi
if ! command -v gh >/dev/null 2>&1; then
    echo "error: GitHub CLI (gh) is required to generate PR-based Release Notes" >&2
    exit 1
fi
if ! gh auth status --hostname github.com >/dev/null 2>&1; then
    echo "error: authenticate GitHub CLI first: gh auth login --hostname github.com" >&2
    exit 1
fi

# The local release tag is the endpoint. It need not have been pushed to GitHub:
# GitHub is consulted only for PR metadata, while the commit range is local.
for ref in "${FROM}" "${TO}"; do
    if ! git rev-parse -q --verify "${ref}^{commit}" >/dev/null; then
        echo "error: '${ref}' is not a commit. Fetch tags first: git fetch --tags" >&2
        exit 1
    fi
done

RANGE="${FROM}..${TO}"
commit_total=$(git rev-list --count "${RANGE}")
if (( commit_total == 0 )); then
    echo "error: no commits in ${RANGE}" >&2
    exit 1
fi

feature=$(mktemp)
bugfix=$(mktemp)
enhance=$(mktemp)
other=$(mktemp)
unassociated=$(mktemp)
trap 'rm -f "${feature}" "${bugfix}" "${enhance}" "${other}" "${unassociated}"' EXIT

# Extract PR numbers from commit subjects, then retrieve all closed PR metadata
# with GitHub REST pagination and select the required merged PRs locally. This
# avoids one request per commit and remains robust when a subject contains an
# upstream issue number that is not a bRPC PR. GitHub-generated merge/squash/
# rebase subjects preserve the final merged PR number. Any commit without one is
# listed separately for human review and is never made into a commit-level note.
# Do not use Bash associative arrays: macOS still ships Bash 3.2.
prs=$(mktemp)
all_prs=$(mktemp)
pr_data_file=$(mktemp)
trap 'rm -f "${feature}" "${bugfix}" "${enhance}" "${other}" "${unassociated}" "${prs}" "${all_prs}" "${pr_data_file}"' EXIT
while IFS=$'\t' read -r sha subject; do
    # Example: "... (#2793) (#3491)". The final #number is the bRPC PR.
    if [[ ${subject} =~ .*\#([0-9]+) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}" >> "${prs}"
    else
        printf '%s %s\n' "${sha:0:12}" "${subject}" >> "${unassociated}"
    fi
done < <(git log --format='%H%x09%s' "${RANGE}")

sort -u "${prs}" -o "${prs}"
pr_total=$(wc -l < "${prs}" | tr -d ' ')
if (( pr_total == 0 )); then
    echo "error: no GitHub PR references found in ${RANGE}; Release Notes must be PR-based" >&2
    exit 1
fi

# GitHub returns up to 100 closed PRs per page. This is dozens of requests for
# the whole repository rather than hundreds of commit/PR requests per release.
echo "Loading merged PR metadata in pages from ${REPO}..." >&2
if ! gh api --paginate "repos/${REPO}/pulls?state=closed&per_page=100" \
    --jq '.[] | select(.merged_at != null) | [.number, .title, .user.login, .merged_at] | @tsv' \
    > "${all_prs}"; then
    echo "error: failed to retrieve merged PR metadata from ${REPO}" >&2
    exit 1
fi

# Both files use lexicographic PR-number ordering required by join, so it can
# select only the PRs in the release range. If a subject's final #number is an
# external issue rather than a bRPC PR, leave it in the review comment instead
# of failing the entire draft.
sort -k1,1 "${all_prs}" -o "${all_prs}"
join -t $'\t' -1 1 -2 1 "${prs}" "${all_prs}" > "${pr_data_file}"
metadata_total=$(grep -cve '^$' "${pr_data_file}" || true)
if (( metadata_total != pr_total )); then
    missing_prs=$(mktemp)
    trap 'rm -f "${feature}" "${bugfix}" "${enhance}" "${other}" "${unassociated}" "${prs}" "${all_prs}" "${pr_data_file}" "${missing_prs}"' EXIT
    join -t $'\t' -v 1 -1 1 -2 1 "${prs}" "${all_prs}" > "${missing_prs}"
    while IFS= read -r missing_pr; do
        printf 'unresolved #PR reference: %s\n' "${missing_pr}" >> "${unassociated}"
    done < "${missing_prs}"
    echo "warning: retrieved ${metadata_total} PR record(s), expected ${pr_total}; unresolved references were left for review" >&2
    pr_total=${metadata_total}
fi
if (( pr_total == 0 )); then
    echo "error: no merged bRPC PR metadata found for ${RANGE}" >&2
    exit 1
fi

while IFS=$'\t' read -r number title author merged_at; do
    if [[ -z ${merged_at} || ${merged_at} == null ]]; then
        echo "error: #${number} is not a merged PR; refusing to include it" >&2
        exit 1
    fi

    lower=$(printf '%s' "${title}" | tr '[:upper:]' '[:lower:]')
    # GitHub Release automatically recognizes @user and #PR-id; do not emit
    # explicit Markdown links. Keep one independent entry per PR: the same
    # author may contribute multiple unrelated features or fixes.
    entry="- ${title} by @${author} (#${number})"
    case ${lower} in
        fix*|*"fix "*|*fixes*|bug*|*"bugfix"*|revert*)
            printf '%s\n' "${entry}" >> "${bugfix}"
            ;;
        feat*|add\ *|"support "*|*"add support"*|"new "*|"implement "*|"introduce "*)
            printf '%s\n' "${entry}" >> "${feature}"
            ;;
        perf*|refactor*|"improve "*|"optimize "*|"speed up"*|"reduce "*|"enhance "*|"clean"*|"remove "*|"update "*|"upgrade "*)
            printf '%s\n' "${entry}" >> "${enhance}"
            ;;
        doc*|test*|ci*|build*|chore*|style*)
            printf '%s\n' "${entry}" >> "${other}"
            ;;
        *)
            printf '%s\n' "${entry}" >> "${enhance}"
            ;;
    esac
done < "${pr_data_file}"

emit() {
    local title=$1 file=$2
    printf '\n%s:\n' "${title}"
    if [[ -s ${file} ]]; then
        cat "${file}"
    else
        printf -- '- (none)\n'
    fi
}

cat <<EOF
<!--
  DRAFT Release Notes for ${RANGE}
  ${pr_total} merged GitHub PR(s), derived from ${commit_total} commit(s).
  Entries are PR-based: one entry per PR, with metadata loaded in paginated batches.
  Buckets are keyword-guessed -- re-file and reword every entry before publishing.
  Produce a Chinese version too; the two go to different channels.
EOF
if [[ -s ${unassociated} ]]; then
    cat <<EOF

  Commits without an associated GitHub PR (${unassociated}):
$(sed 's/^/    /' "${unassociated}")
  Review them manually; do not turn them into commit-level Release Notes.
EOF
fi
cat <<EOF
-->

[Release Notes]
EOF

emit "Feature"     "${feature}"
emit "Bugfix"      "${bugfix}"
emit "Enhancement" "${enhance}"
emit "Other"       "${other}"

cat <<EOF

<!--
Full comparison: https://github.com/${REPO}/compare/${FROM}...${TO}
PR search: https://github.com/${REPO}/pulls?q=is%3Apr+is%3Amerged
-->
EOF
