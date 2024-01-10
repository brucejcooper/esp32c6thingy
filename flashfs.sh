#!/usr/bin/env sh

DIR=/tmp/espcoap_tmp_img
BINFILE=build/storage.bin
PORT=/dev/tty.usbserial-111110
IP="fdbf:1afc:5480:1:30a3:bef2:6c55:fccd"

rm -rf $DIR
mkdir $DIR
# Copy default files. 
cp scripts/{init,init_ot,file_server,router,async,helpers}.lua $DIR

case $1 in
    bridge)
        echo "Configuring Bridge"
        cp scripts/{dali}.lua $DIR
        cp scripts/init_dali_bridge.lua $DIR/init.lua        
        ;;
    switch)
        echo "Configuring Switch"
        cp scripts/{button,button_actions}.lua $DIR
        cp scripts/init_buttons.lua $DIR/init.lua
        PORT=/dev/tty.usbserial-1130
        IP="fdbf:1afc:5480:1:8a08:4ae9:e400:964d"
        ;;
    *)
        echo "Setting up device as default"
        ;;
esac

case $2 in
    sync)
        python3 sync.py $IP
        ;;
    flash)
        $HOME/esp/esp-idf/components/spiffs/spiffsgen.py 0xF0000 $DIR $BINFILE
        esptool.py --chip esp32c6 -p $PORT -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x10000 $BINFILE
        idf.py -p $PORT monitor
        ;;
    *)
        echo "You should specify a sync or a flash as the second option"
        ;;
esac
