local log = Logger:new("lua_init")

-- require("button")
require("router")
require("file_server")
require("dali")



-- local function toggle_device(b)
--     log:info("Button toggled", b.pin)
-- end

-- local function dim_device(b)
--     log:info("Button dimmed", b.pin)
-- end

-- This device has 5 buttons
-- Button:new{
--     pin=19,
--     target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
--     on_click=toggle_device,
--     on_long_press=dim_device
-- }
-- Button:new{
--     pin = 20,
--     target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
--     on_click=toggle_device,
--     on_long_press=dim_device
-- }
-- Button:new{
--     pin = 21,
--     target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
--     on_click=toggle_device,
--     on_long_press=dim_device
-- }
-- Button:new{
--     pin = 22,
--     target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
--     on_click=toggle_device,
--     on_long_press=dim_device
-- }
-- Button:new{
--     pin = 23,
--     target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
--     on_click=toggle_device,
--     on_long_press=dim_device
-- }

-- log:info("Starting DALI")
-- Dali:new(4,5)
log:info("LUA system script completed")