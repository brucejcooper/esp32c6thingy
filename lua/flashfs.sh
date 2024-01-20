#!/usr/bin/env sh

DIR=/tmp/espcoap_tmp_img
BINFILE=build/storage.bin

DEVCLASS=$1
IP=$2
PORT=$3
SYNCTYPE=$4

rm -rf $DIR
mkdir $DIR
# Copy default files. 
cp shared/* $DIR
# Also copy device type
cp $DEVCLASS/* $DIR
# Also copy device specific
cp devices/$IP/* $DIR

case $SYNCTYPE in
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
