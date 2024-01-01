-- require("dali_bus");
require("button")


local function toggle_device(b)
    log.info("Button toggled", b.pin)
end

local function dim_device(b)
    log.info("Button dimmed", b.pin)
end


-- This device has 5 buttons
Button:new{
    pin=19,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
    on_click=toggle_device,
    on_long_press=dim_device
}
Button:new{
    pin = 20,
    target_device="coap://fdbf:1afc:5480:1:30a3:bef2:6c55:fccd/d/abc1234",
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


coap.resource("info", function(req)
    if req.code == coap.CODE_GET then
        log.info("Get of test resource")
        local resp = {
            body="Hello World"
        }
        return resp
    else
        return { code=coap.CODE_METHOD_NOT_ALLOWED }
    end
end)

coap.resource("restart", function(req)
    if req.code == coap.CODE_POST then
        log.info("Restarting system");
        system.restart()
    else
        return { code=coap.CODE_METHOD_NOT_ALLOWED }
    end
end)


local dali = DaliBus:new(4,5)
log.warn("Dali bus is ", dali, getmetatable(dali))


local function serve_device(req)
    if req.code == coap.CODE_GET then
        local addr = tonumber(req.path[#req.path])
        log.info("Getting level of device", addr)
        -- The dali fetch needs to run in a co-routine, as it blocks waiting on the driver
        -- This means we will transmit two UDP packets - one with the initial ACK, and another with the content.
        system.start_coro(function()
            log.info("Started Coro to process DALI request")
            local level = dali:query_actual(addr)
            coap.reply(req, {
                body={ level=level } -- This will be CBOR encoded
            })
        end)
        -- This handler will return nothing. It will acknowledge immediately with an EMPTY response, then send the real response from the co-routine
    else
        return { code=coap.CODE_METHOD_NOT_ALLOWED }
    end

end


system.start_coro(function()
    log.info("Doing initial scan of Dali devices")
    for addr=0,63 do
        local res = dali:query_actual(addr)
        if res >= 0 then
            log.info("Address", addr, "level is", res)
            coap.resource(string.format("dali/%d", addr), serve_device)
        end
    end
end)
