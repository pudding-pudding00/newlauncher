#!/usr/bin/env bash
# Release gate, run by release.yml before anything is published: the tagged commit must be on main, and
# every workflow run that its push to main started (CI, CodeQL) must have finished successfully. Waits while
# those runs are still going, as they are when a tag is pushed right after a merge.
#
# Usage: tools/release_gate.sh <commit> [wait-minutes, default 90]
# Needs git (a clone that can fetch origin) and an authenticated gh.
set -euo pipefail

SHA=$(git rev-parse "${1:?usage: release_gate.sh <commit> [wait-minutes]}^{commit}")
WAIT_MIN=${2:-90}
REPO=${GITHUB_REPOSITORY:-$(gh repo view --json nameWithOwner --jq .nameWithOwner)}

git fetch -q origin main
if ! git merge-base --is-ancestor "$SHA" FETCH_HEAD; then
  echo "::error::$SHA is not on main: releases are made from main only"
  exit 1
fi

# The workflow runs started by pushing this commit to main: CI ("push") and CodeQL code scanning
# ("dynamic"). The release workflow's own runs are left out: it is the one asking.
RUNS='[.workflow_runs[] | select((.event == "push" or .event == "dynamic") and .head_branch == "main"
                                and .path != ".github/workflows/release.yml")]'
runs() { gh api "repos/$REPO/actions/runs?head_sha=$SHA&per_page=100" --jq "$RUNS | $1"; }

deadline=$(( $(date +%s) + WAIT_MIN * 60 ))
while :; do
  ci=$(runs 'map(select(.path == ".github/workflows/ci.yml")) | length')
  pending=$(runs 'map(select(.status != "completed")) | length')
  [ "$ci" -gt 0 ] && [ "$pending" -eq 0 ] && break
  if [ "$(date +%s)" -ge "$deadline" ]; then
    if [ "$ci" -eq 0 ]; then
      echo "::error::no CI run for the push of $SHA to main"
    else
      echo "::error::$pending check run(s) for $SHA still unfinished after $WAIT_MIN min"
    fi
    exit 1
  fi
  echo "waiting for the checks of $SHA on main (CI runs: $ci, unfinished: $pending)"
  sleep 60
done

failed=$(runs 'map(select(.conclusion != "success" and .conclusion != "skipped"))
               | map("\(.name): \(.conclusion) \(.html_url)") | .[]')
if [ -n "$failed" ]; then
  echo "::error::checks of $SHA on main did not all pass"
  echo "$failed"
  exit 1
fi
echo "all checks of $SHA on main passed:"
runs 'map("  \(.name): \(.conclusion)") | .[]'
