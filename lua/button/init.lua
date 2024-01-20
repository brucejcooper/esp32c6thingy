-- Start Openthread, and get a standard set of handlers going.
require("init_ot"):start()
require("coap"):start_server()
require("default_handlers")

-- Code below here is device-specific
require("button")



local dali_bridge_ip = openthread.parse_ip6("fdbf:1afc:5480:1:30a3:bef2:6c55:fccd")
---Glue function to create an event handler that will send the supplied action to a dali bridge device.
local function send_dali_cmd(action)
    return function(button)
        coap:send_non_confirmable {
            code = "post",
            peer_addr = dali_bridge_ip,
            path = { "dali", tostring(button.dali_addr), action }
        }
    end
end

--This device has 5 buttons
Button:new {
    pin = 19,
    dali_addr = 1,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("dim"),
}

Button:new {
    pin = 20,
    dali_addr = 2,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("dim"),
}
Button:new {
    pin = 21,
    dali_addr = 3,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("dim"),
}
Button:new {
    pin = 22,
    dali_addr = 4,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("dim"),
}
Button:new {
    pin = 23,
    dali_addr = 5,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("dim"),
}
