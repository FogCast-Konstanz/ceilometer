#!/usr/bin/env bash
# Called only by CI after tests/build succeed. Deployment history is fast-forward only.
set -euo pipefail
bundle="$1"
publish_dir="$(mktemp -d)"
tar -xzf "$bundle" -C "$publish_dir"
git config user.name 'Ceilometer CI'
git config user.email 'ceilometer-ci@users.noreply.github.com'
parent=()
if git ls-remote --exit-code origin refs/heads/deploy >/dev/null 2>&1; then
  git fetch origin refs/heads/deploy:refs/remotes/origin/deploy
  parent=(-p "$(git rev-parse refs/remotes/origin/deploy)")
fi
export GIT_INDEX_FILE="$RUNNER_TEMP/ceilometer-deployment.index"
git --work-tree="$publish_dir" add --all
tree="$(git --work-tree="$publish_dir" write-tree)"
commit="$(printf 'Deploy source %s\n' "$GITHUB_SHA" | git commit-tree "$tree" "${parent[@]}")"
git push origin "$commit:refs/heads/deploy"
