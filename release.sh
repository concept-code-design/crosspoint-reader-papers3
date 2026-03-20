#!/usr/bin/env bash
set -euo pipefail

# release.sh — Bump patch version, build, commit, push, and create GitHub release
# Usage: ./release.sh [optional commit message suffix]

INI="platformio.ini"
FIRMWARE=".pio/build/default/firmware.bin"

# 1. Read current version
CURRENT=$(sed -n 's/^version = //p' "$INI")
if [[ -z "$CURRENT" ]]; then
  echo "ERROR: Could not read version from $INI"
  exit 1
fi

# 2. Bump patch
IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"
PATCH=$((PATCH + 1))
NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"

echo "==> Bumping version: $CURRENT -> $NEW_VERSION"
sed -i '' "s/^version = .*/version = $NEW_VERSION/" "$INI"

# 3. Build firmware
echo "==> Building firmware..."
pio run -e default
if [[ ! -f "$FIRMWARE" ]]; then
  echo "ERROR: Build succeeded but $FIRMWARE not found"
  exit 1
fi

# 4. Commit version bump + any staged changes
echo "==> Committing..."
SUFFIX="${1:-}"
MSG="Release $NEW_VERSION"
[[ -n "$SUFFIX" ]] && MSG="$MSG — $SUFFIX"
git add -A
git commit -m "$MSG"

# 5. Push
echo "==> Pushing to origin..."
git push origin master

# 6. Create GitHub release with firmware.bin
echo "==> Creating GitHub release $NEW_VERSION..."
gh release create "$NEW_VERSION" "$FIRMWARE" \
  --target master \
  --title "$NEW_VERSION" \
  --generate-notes

echo ""
echo "==> Done! Release $NEW_VERSION published."
echo "    https://github.com/juicecultus/crosspoint-reader-papers3/releases/tag/$NEW_VERSION"
