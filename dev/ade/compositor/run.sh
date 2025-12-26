
#!/bin/sh
# Hard safety net for compositor dev

# Ensure terminal sane before start
stty sane

# Run compositor
./build/ade-compositor

# Always restore terminal, even if it crashes
stty sane

