#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RULE_SOURCE="$SCRIPT_DIR/../udev/70-compx-mouse-battery.rules"
RULE_TARGET="/etc/udev/rules.d/70-compx-mouse-battery.rules"

if [[ ! -f "$RULE_SOURCE" ]]; then
    echo "未找到 udev 规则：$RULE_SOURCE" >&2
    exit 1
fi

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo -- "$SCRIPT_DIR/install-udev-rule.sh" "$@"
fi

install -Dm644 "$RULE_SOURCE" "$RULE_TARGET"
udevadm control --reload-rules
udevadm trigger --subsystem-match=hidraw

echo "已安装 $RULE_TARGET"
echo "请重新插拔 2.4G 接收器，然后重新打开 MouseBattery。"
