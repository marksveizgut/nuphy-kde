#!/bin/sh
set -eu
cd "$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
if [ "$(id -u)" -eq 0 ]; then
    echo 'Install the widget as your desktop user, without sudo.' >&2
    exit 1
fi
if kpackagetool6 --type Plasma/Applet --show local.nuphy.lighting >/dev/null 2>&1; then
    kpackagetool6 --type Plasma/Applet --upgrade widget
else
    kpackagetool6 --type Plasma/Applet --install widget
fi
echo 'Right-click the panel, enter Edit Mode, and add the NuPhy Lighting widget.'
