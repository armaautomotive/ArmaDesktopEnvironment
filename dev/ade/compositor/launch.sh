
cd ~/dev/ade/compositor

export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WLR_RENDERER=pixman
./build/ade-compositor

