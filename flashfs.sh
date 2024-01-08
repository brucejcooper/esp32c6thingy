#!/usr/bin/env sh

DIR=/tmp/espcoap_tmp_img
BINFILE=build/storage.bin
PORT=/dev/tty.usbserial-111110

rm -rf $DIR
mkdir $DIR
cp scripts/{init,init_ot,file_server,router,event_loop}.lua $DIR
$HOME/esp/esp-idf/components/spiffs/spiffsgen.py 0xF0000 $DIR $BINFILE
esptool.py --chip esp32c6 -p $PORT -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x10000 $BINFILE
