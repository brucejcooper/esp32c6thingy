#!/usr/bin/env sh

DIR=/tmp/espcoap_tmp_img
BUILDDIR=../firmware/build
BINFILE=$BUILDDIR/storage.bin

DEVID=$1
case $DEVID in

    button)
        IP="fdbf:1afc:5480:1:8a08:4ae9:e400:964d"
        PORT=/dev/tty.usbserial-11130
        DEVCLASS="button"
        ;;

    dali)
        IP="fdbf:1afc:5480:1:30a3:bef2:6c55:fccd"
        PORT=/dev/tty.usbserial-1111110
        DEVCLASS="dali"
        ;;

esac

SYNCTYPE=$2

rm -rf $DIR
mkdir $DIR
# Copy default files. 
cp shared/* $DIR
# Also copy device type
cp $DEVCLASS/* $DIR
# Also copy device specific
cp devices/$DEVID/* $DIR

case $SYNCTYPE in
    sync)
        python3 sync.py $IP
        # idf.py -B $BUILDDIR -p $PORT monitor
        ;;
    flash)
        $HOME/esp/esp-idf/components/spiffs/spiffsgen.py 0xF0000 $DIR $BINFILE
        esptool.py --chip esp32c6 -p $PORT -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x10000 $BINFILE
        idf.py -B $BUILDDIR -p $PORT monitor
        ;;
    *)
        echo "You should specify a sync or a flash as the second option"
        ;;
esac
