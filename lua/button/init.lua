-- Start Openthread, and get a standard set of handlers going.
local log = Logger:get("main");
require("init_ot"):start()
require("coap"):start_server()
require("default_handlers")

-- Code below here is device-specific
require("button")

print("Ca cert is ", openthread.certs.ca)
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

local function fetch_dali_level(button)
    start_async_task(function()
        local resp = await(coap:send_confirmable {
            code = "get",
            peer_addr = dali_bridge_ip,
            path = { "dali", tostring(button.dali_addr) }
        })
        if resp.code ~= "content" then
            error("Didn't get content from dali device")
        end
        local p = cbor.decode(resp.payload)
        log:info("current level for device ", button.dali_addr, "is", p)
    end)
end


--Button1 controls a relay, which can't be dimmed.  Toggle as soon as the buttong is pressed
Button:new {
    pin = 19,
    dali_addr = 0,
    on_press = send_dali_cmd("toggle"),
}

-- Button 2 is a dimmable DALI device.
Button:new {
    pin = 20,
    dali_addr = 1,
    on_press = fetch_dali_level,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("down"),
}
Button:new {
    pin = 21,
    dali_addr = 3,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("down"),
}
Button:new {
    pin = 22,
    dali_addr = 4,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("down"),
}
Button:new {
    pin = 23,
    dali_addr = 5,
    on_click = send_dali_cmd("toggle"),
    on_long_press = send_dali_cmd("down"),
}


require("crypto")

local private, public = crypto.ec_keypair_gen()
print("Private", Helpers.hexlify(private), string.len(private), "Public", Helpers.hexlify(public), string.len(public));
