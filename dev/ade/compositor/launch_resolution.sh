#!/bin/sh

if [ -x "./build/ade-resolution" ]; then
    exec ./build/ade-resolution
fi

msg="ADE Resolution utility is not built.\n\nInstall gtk3 and wlr-randr in the VM, then rebuild:\n\nsudo pacman -S --needed gtk3 wlr-randr\nmeson compile -C build"

if command -v foot >/dev/null 2>&1; then
    exec foot -T ADE-Resolution -e /bin/sh -lc "printf '$msg\n\nPress Enter to close.'; read _"
fi

printf '%b\n' "$msg"
