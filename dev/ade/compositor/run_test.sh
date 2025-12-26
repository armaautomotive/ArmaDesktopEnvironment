

rm -f /tmp/ade.log /tmp/ade.exit
( WLR_LOG=debug WLR_NO_HARDWARE_CURSORS=1 ./build/ade-compositor ) > /tmp/ade.log 2>&1
echo $? > /tmp/ade.exit
tail -n 120 /tmp/ade.log
echo "exit code: $(cat /tmp/ade.exit)"


