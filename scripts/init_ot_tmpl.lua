-- This is a template file - used as a hint as to how openthread can be configured. 
-- the real init_ot.lua isn't checked into git because it contains secrets.  To confgure
-- your device, copy this file into init_ot.lua, update it with your configuration, and
-- flash the fs of your device.

openthread = OpenThread:init{
    channel=15,
    -- This is the raw binary value of the network key - the real secret here
    network_key= "\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff",
    -- The network name - replace with your own
    network_name= "NETNAME",
    -- The pan_id of your network
    pan_id= 0x1122,
    ext_pan_id= "\x11\x22\x33\x44\x55\x66\x77\x88",
    -- Unlike the other values in this file, this is a string representation
    -- We do that because the full 128 bit version is very long
    mesh_local_prefix="fd11:1234:1234:1234::/64"

    -- Channel Mask: 0x07fff800
    -- PSKc: fdbc10f0efb7809768d1404cd7fa9f68
    -- Security Policy: 672 onrc 0
}
openthread:start()