-- A sample init file that will be installed on a button like device.
-- require("init_ot") -- Start Openthread
require("file_server")
require("button")
require("button_actions")

--This device has 5 buttons
Button:new{
    pin=19,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/dali/1",
    on_click=toggle_device,
    on_long_press=dim_device
}
Button:new{
    pin = 20,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/dali/0",
    on_click=toggle_device,
    on_long_press=dim_device
}
Button:new{
    pin = 21,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
    on_click=toggle_device,
    on_long_press=dim_device
}
Button:new{
    pin = 22,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
    on_click=toggle_device,
    on_long_press=dim_device
}
Button:new{
    pin = 23,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
    on_click=toggle_device,
    on_long_press=dim_device
}
