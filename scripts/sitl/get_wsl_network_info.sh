#!/usr/bin/env bash

set -u

section() {
	printf '\n%s\n' "$1"
}

kernel_release="$(uname -r 2>/dev/null || true)"
if [[ "$kernel_release" != *microsoft-standard-WSL2* ]]; then
	printf 'WARNING: this does not appear to be WSL2 (kernel: %s).\n' "$kernel_release"
fi

wsl_ipv4="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "src") {print $(i + 1); exit}}')"
windows_gateway="$(ip -4 route show default 2>/dev/null | awk '/default/ {print $3; exit}')"

section "WSL2 network"
printf 'WSL IPv4:                 %s\n' "${wsl_ipv4:-unavailable}"
printf 'Windows host/gateway:     %s\n' "${windows_gateway:-unavailable}"
printf 'Kernel:                   %s\n' "$kernel_release"

section "PX4 MAVLink endpoints"
printf 'GCS instance:             UDP local 18570, nominal remote 14550\n'
printf 'Onboard instance:         UDP local 14580, nominal remote 14540\n'
printf 'Low-rate onboard:         UDP local 14280, nominal remote 14030\n'
printf 'Gimbal instance:          UDP local 13030, nominal remote 13280\n'

if command -v ss >/dev/null 2>&1; then
	printf '\nActive relevant UDP sockets:\n'
	ss -lunp 2>/dev/null | awk 'NR == 1 || $5 ~ /:(13030|13280|14030|14280|14540|14550|14580|18570)$/ {print}'
else
	printf '\nWARNING: ss is unavailable; live UDP sockets were not inspected.\n'
fi

section "Expected Windows QGroundControl configuration"
printf 'Link name:                PX4 WSL SITL\n'
printf 'Link type:                UDP\n'
printf 'Server address:           %s\n' "${wsl_ipv4:-<current WSL IPv4>}"
printf 'Server port:              18570\n'
printf 'QGC listening port:       14550 (default)\n'
printf '\nThe WSL IPv4 can change after WSL restarts; rerun this script before connecting.\n'
printf 'Confirm the live values in the PX4 shell with: mavlink status\n'
