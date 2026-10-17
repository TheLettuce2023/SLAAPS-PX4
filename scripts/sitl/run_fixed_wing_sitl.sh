#!/usr/bin/env bash

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
repo_fs="$(findmnt -T "$repo_root" -n -o FSTYPE 2>/dev/null || true)"

if [[ "$repo_fs" != 9p && "$repo_fs" != drvfs && "$repo_fs" != fuseblk ]]; then
	printf 'Launching stock PX4 fixed-wing baseline from %s\n' "$repo_root"
	printf '+ make px4_sitl gz_rc_cessna\n'
	cd "$repo_root"
	exec make px4_sitl gz_rc_cessna
fi

# PX4/CMake performs writes that DrvFS cannot represent reliably. Keep the
# checked-out source in place, but bind its generated build and VS Code output
# paths to the native Linux filesystem. This is deliberately non-destructive.
cache_root="${SLAAPS_PX4_CACHE_DIR:-$HOME/.cache/slaaps-px4}"
mount_root="${SLAAPS_PX4_MOUNT_ROOT:-/tmp/slaaps-px4-mount}"
native_build="$cache_root/build"
native_vscode="$cache_root/source-overlays/vscode"

mkdir -p "$native_build" "$native_vscode"
if [[ -d "$repo_root/.vscode" && ! -e "$native_vscode/.initialized" ]]; then
	cp -a "$repo_root/.vscode/." "$native_vscode/"
	touch "$native_vscode/.initialized"
fi

sudo mkdir -p "$mount_root"
if mountpoint -q "$mount_root"; then
	if [[ ! "$repo_root/.git/config" -ef "$mount_root/.git/config" ]]; then
		printf 'ERROR: %s is already mounted from another source; refusing to replace it.\n' "$mount_root" >&2
		exit 1
	fi
else
	printf 'Creating source bind mount: %s -> %s\n' "$mount_root" "$repo_root"
	sudo mount --bind "$repo_root" "$mount_root"
fi

sudo mkdir -p "$mount_root/build" "$mount_root/.vscode"
if ! mountpoint -q "$mount_root/build"; then
	printf 'Creating native build bind mount: %s -> %s/build\n' "$native_build" "$mount_root"
	sudo mount --bind "$native_build" "$mount_root/build"
fi
if ! mountpoint -q "$mount_root/.vscode"; then
	printf 'Creating native metadata bind mount: %s -> %s/.vscode\n' "$native_vscode" "$mount_root"
	sudo mount --bind "$native_vscode" "$mount_root/.vscode"
fi

if ! git -C "$mount_root" status --short >/dev/null 2>&1; then
	printf 'ERROR: Git does not trust the bind-mounted checkout. Add only this exact path:\n' >&2
	printf '  git config --global --add safe.directory %q\n' "$mount_root" >&2
	exit 1
fi

printf 'Source is on %s; generated build output is on the native Linux filesystem.\n' "$repo_fs"
printf 'Launching stock PX4 fixed-wing baseline.\n'
printf '+ make px4_sitl gz_rc_cessna\n'
cd "$mount_root"
exec make px4_sitl gz_rc_cessna
