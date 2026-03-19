#!/bin/sh

if [ -x "./build/ade-icon-command" ]; then
    exec ./build/ade-icon-command "$1" "$2" "$3"
fi

msg="ADE Icon Command utility is not built.\n\nInstall gtk3 in the VM, then rebuild:\n\nsudo pacman -S --needed gtk3\nmeson compile -C build"

if command -v foot >/dev/null 2>&1; then
    exec foot -T ADE-Icon-Command -e /bin/sh -lc "printf '$msg\n\nPress Enter to close.'; read _"
fi

printf '%b\n' "$msg"
