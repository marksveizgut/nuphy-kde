#!/bin/sh
set -eu
cd "$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run make first, then sudo ./install.sh' >&2
    exit 1
fi
if [ ! -x build/nuphy-backlight ]; then
    echo 'Build the executable with make as your normal user first.' >&2
    exit 1
fi
# Verify the exact keyboard and protocol before installing/enabling anything.
# On upgrades, release the existing service's advisory HID lock first, and
# restore it automatically if the probe or installation fails.
if systemctl is-active --quiet nuphy-backlight.service; then
    systemctl stop nuphy-backlight.service
    trap 'systemctl start nuphy-backlight.service' 0
fi
./build/nuphy-backlight --probe
modprobe uleds
install -Dm755 build/nuphy-backlight /usr/local/bin/nuphy-backlight
install -Dm644 packaging/nuphy-backlight.service /etc/systemd/system/nuphy-backlight.service
install -Dm644 packaging/nuphy-backlight.conf /etc/modules-load.d/nuphy-backlight.conf
install -Dm644 packaging/local.NuPhy.Backlight.conf /etc/dbus-1/system.d/local.NuPhy.Backlight.conf
systemctl daemon-reload
systemctl reload dbus.service
systemctl enable nuphy-backlight.service
systemctl restart nuphy-backlight.service
trap - 0
# UPower versions differ in whether they discover virtual LEDs dynamically.
systemctl restart upower.service
echo 'Installed and started. Run this as your desktop user:'
echo '  systemctl --user restart plasma-powerdevil.service'
echo 'Then open Brightness & Color in the KDE system tray.'
