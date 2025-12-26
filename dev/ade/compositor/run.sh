
#!/bin/sh
# Hard safety net for compositor dev

# Ensure terminal sane before start
stty sane

export XCURSOR_THEME=haiku
export XCURSOR_SIZE=32
export XCURSOR_PATH="$PWD/cursors:$XCURSOR_PATH"

# Run compositor
./build/ade-compositor

# Always restore terminal, even if it crashes
stty sane

