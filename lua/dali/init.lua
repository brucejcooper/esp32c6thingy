-- Start Openthread, and get a standard set of handlers going.
require("init_ot"):start()
require("coap"):start_server()
require("default_handlers")


-- Application specific
require("dali")


local log = Logger:get("main")

log:info("Starting DALI")
Dali:new(4, 5)
