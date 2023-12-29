require("devices")

dali_bus = { }

function dali_bus:new(rx_pin, tx_pin)
    local b = { 
        id = string.format("sn:abc1234:pin:%d", tx_pin),
        desc = "Controller for the DALI bus",
        attr = {}, 
        rx_pin = rx_pin, 
        tx_pin = tx_pin 
    }
    setmetatable(b, self)
    self.__index = self

    register_device(b)
    return b
end

function dali_bus:set_attr(values)
    return {
        body = "This device has no settable attributes",
        code = 406,
    }
end

function dali_bus:send_cmd(addr, cb) 
    error("Not implemented")
end



function dali_bus:scan()
    for addr:0,64 do
        print("Scanning", addr)
    end
    return {
        code = 204
    }
end

function dali_bus:init()
    return {
        body = "This operation is not yet defined",
        code = 501,
    }
end


return dali_bus