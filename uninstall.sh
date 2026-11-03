#!/bin/sh
set -eu
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run sudo ./uninstall.sh' >&2
    exit 1
fi
systemctl disable --now nuphy-backlight.service
rm -f /usr/local/bin/nuphy-backlight /etc/systemd/system/nuphy-backlight.service /etc/modules-load.d/nuphy-backlight.conf
rm -f /etc/dbus-1/system.d/local.NuPhy.Backlight.conf
systemctl daemon-reload
systemctl reload dbus.service
systemctl restart upower.service
echo 'Removed. Run systemctl --user restart plasma-powerdevil.service as your desktop user.'
