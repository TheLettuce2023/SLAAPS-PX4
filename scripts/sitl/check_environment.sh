#!/usr/bin/env bash

set -u

passes=0
warnings=0
failures=0

pass() {
	passes=$((passes + 1))
	printf 'PASS: %s\n' "$*"
}

warn() {
	warnings=$((warnings + 1))
	printf 'WARNING: %s\n' "$*"
}

fail() {
	failures=$((failures + 1))
	printf 'FAIL: %s\n' "$*"
}

have() {
	command -v "$1" >/dev/null 2>&1
}

kernel_release="$(uname -r 2>/dev/null || true)"
if [[ "$kernel_release" == *microsoft-standard-WSL2* ]]; then
	pass "WSL2 kernel detected ($kernel_release)"
else
	fail "WSL2 kernel not detected ($kernel_release)"
fi

if [[ -r /etc/os-release ]]; then
	# shellcheck disable=SC1091
	. /etc/os-release
	if [[ "${ID:-}" == ubuntu ]]; then
		pass "Ubuntu ${VERSION_ID:-unknown} (${PRETTY_NAME:-unknown})"
	else
		fail "Ubuntu is required; detected ${PRETTY_NAME:-unknown}"
	fi
else
	fail "/etc/os-release is unavailable"
fi

repo_root="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "$repo_root" ]]; then
	fail "current directory is not inside a Git repository"
	printf '\nSummary: %d PASS, %d WARNING, %d FAIL\n' "$passes" "$warnings" "$failures"
	exit 1
fi

repo_fs="$(findmnt -T "$repo_root" -n -o FSTYPE 2>/dev/null || true)"
case "$repo_fs" in
	ext4|xfs|btrfs)
		pass "repository is on native Linux filesystem $repo_fs ($repo_root)"
		;;
	9p|drvfs|fuseblk)
		warn "repository is on Windows-mounted filesystem $repo_fs ($repo_root); PX4 builds are slower and require the native build overlay used by run_fixed_wing_sitl.sh"
		;;
	*)
		warn "repository filesystem could not be classified (${repo_fs:-unknown})"
		;;
esac

branch="$(git -C "$repo_root" branch --show-current 2>/dev/null || true)"
if [[ "$branch" == slaaps-sitl ]]; then
	pass "development branch is slaaps-sitl"
else
	warn "active branch is ${branch:-detached}; expected slaaps-sitl for project work"
fi

origin_url="$(git -C "$repo_root" remote get-url origin 2>/dev/null || true)"
upstream_url="$(git -C "$repo_root" remote get-url upstream 2>/dev/null || true)"
if [[ "$origin_url" =~ github\.com[:/]TheLettuce2023/SLAAPS-PX4(\.git)?$ ]]; then
	pass "origin points to the project fork ($origin_url)"
else
	fail "origin is not the expected project fork (${origin_url:-missing})"
fi
if [[ "$upstream_url" =~ github\.com[:/]PX4/PX4-Autopilot(\.git)?$ ]]; then
	pass "upstream points to PX4/PX4-Autopilot ($upstream_url)"
else
	fail "upstream is not PX4/PX4-Autopilot (${upstream_url:-missing})"
fi

if have gh; then
	origin_slug="$(printf '%s' "$origin_url" | sed -E 's#^(https://github.com/|git@github.com:)##; s#\.git$##')"
	fork_metadata="$(gh repo view "$origin_slug" --json isFork,parent,owner,name --jq '[.isFork, .parent.owner.login, .parent.name, .owner.login, .name] | @tsv' 2>/dev/null || true)"
	if [[ "$fork_metadata" == $'true\tPX4\tPX4-Autopilot\tTheLettuce2023\tSLAAPS-PX4' ]]; then
		pass "GitHub confirms origin is TheLettuce2023/SLAAPS-PX4 forked from PX4/PX4-Autopilot"
	else
		warn "GitHub fork metadata could not be confirmed (offline, unauthenticated, or unexpected metadata)"
	fi
else
	warn "GitHub CLI is unavailable; fork metadata was not checked"
fi

submodule_status="$(git -C "$repo_root" submodule status --recursive 2>/dev/null || true)"
if [[ -z "$submodule_status" ]]; then
	fail "submodule status is empty"
elif printf '%s\n' "$submodule_status" | awk 'substr($0,1,1) ~ /[-+U]/ {bad=1} END {exit !bad}'; then
	fail "one or more submodules are missing, conflicted, or not at the pinned revision"
else
	pass "recursive submodules are initialized at pinned revisions"
fi

for tool in git cmake ninja python3 make; do
	if have "$tool"; then
		version="$($tool --version 2>/dev/null | head -n 1)"
		pass "$tool available (${version:-version unavailable})"
	else
		fail "$tool is unavailable"
	fi
done

if have gz; then
	gz_version="$(gz sim --versions 2>/dev/null | head -n 1)"
	if [[ "$gz_version" =~ ^8\. ]]; then
		pass "Gazebo Sim ${gz_version} is available"
	else
		warn "Gazebo Sim version is ${gz_version:-unavailable}; this checkout requires Gazebo Sim 8+"
	fi
else
	fail "Gazebo command 'gz' is unavailable"
fi

if [[ -n "${DISPLAY:-}" && -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]]; then
	if [[ -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
		pass "WSLg display variables and Wayland socket are available"
	else
		warn "WSLg variables exist, but ${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY} is not a socket"
	fi
else
	warn "WSLg display environment is incomplete (DISPLAY/WAYLAND_DISPLAY/XDG_RUNTIME_DIR)"
fi

wsl_ipv4="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "src") {print $(i + 1); exit}}')"
gateway="$(ip -4 route show default 2>/dev/null | awk '/default/ {print $3; exit}')"
if [[ -n "$wsl_ipv4" && -n "$gateway" ]]; then
	pass "WSL networking is available (guest $wsl_ipv4, Windows host/gateway $gateway)"
else
	warn "WSL IPv4 or Windows host/gateway could not be determined"
fi

if git -C "$repo_root" diff --quiet && git -C "$repo_root" diff --cached --quiet; then
	if [[ -z "$(git -C "$repo_root" status --porcelain --untracked-files=normal)" ]]; then
		pass "Git worktree is clean"
	else
		warn "Git worktree contains untracked files"
	fi
else
	warn "Git worktree contains tracked changes"
fi

printf '\nSummary: %d PASS, %d WARNING, %d FAIL\n' "$passes" "$warnings" "$failures"
if ((failures > 0)); then
	exit 1
fi
