#!/usr/bin/env bash
#
# Fetch the default branch of upstream NetRadiant-custom and rebase the
# currently checked out branch onto it.
#
# Usage: ./rebase-upstream.sh [extra git-rebase arguments...]
#
# The upstream branch is stored as `upstream/<branch>` (a remote-tracking
# ref) without having to configure an `upstream` remote. The repository URL
# can be overridden with the UPSTREAM_URL environment variable.

set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/Garux/netradiant-custom}"

die() {
	echo "error: $*" >&2
	exit 1
}

# Operate on the repository this script lives in, regardless of the cwd.
cd "$(git -C "$(dirname "$(readlink -f "$0")")" rev-parse --show-toplevel)"

# A rebase needs a named branch to move.
current_branch="$(git symbolic-ref --quiet --short HEAD)" \
	|| die "HEAD is detached, check out the branch to rebase first"

# Refuse to start with uncommitted changes to tracked files, so nothing gets
# mixed into (or lost during) the rebase. Untracked files are fine.
git update-index -q --refresh
if ! git diff --quiet || ! git diff --cached --quiet; then
	die "working tree has uncommitted changes, commit or stash them first"
fi

# Ask upstream which branch its HEAD points to instead of hardcoding it.
upstream_branch="$(git ls-remote --symref "$UPSTREAM_URL" HEAD \
	| awk '$1 == "ref:" && $3 == "HEAD" { sub("^refs/heads/", "", $2); print $2 }')"
[ -n "$upstream_branch" ] || die "could not determine default branch of $UPSTREAM_URL"

upstream_ref="refs/remotes/upstream/$upstream_branch"

echo "Fetching $upstream_branch from $UPSTREAM_URL ..."
git fetch --no-tags "$UPSTREAM_URL" "+refs/heads/$upstream_branch:$upstream_ref"

# Give a quick overview of what the rebase is going to do.
behind="$(git rev-list --count "HEAD..$upstream_ref")"
ahead="$(git rev-list --count "$upstream_ref..HEAD")"
echo "Rebasing $current_branch ($ahead own commits) onto upstream/$upstream_branch ($behind new commits) ..."

git rebase "$@" "$upstream_ref"
