require("init_ot") -- Start Openthread
require("file_server")
require("dali")

local log = Logger:get("main")

log:info("Starting DALI")
Dali:new(4,5)
