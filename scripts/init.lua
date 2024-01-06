-- This is a default init file that is only used when building out starting firmware.
-- It will not start anything by 
require("init_ot") -- Start Openthread
require("file_server") -- Register a handler that does file system stuff so that we can interact with the device and further configure it
local log = Logger:new("main")
log:warn("This device is running in a default configuration")