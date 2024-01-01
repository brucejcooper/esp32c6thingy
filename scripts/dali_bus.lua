require("devices")

DaliBus = { }

function DaliBus:new(rx_pin, tx_pin)
    local b = { 
        rx_pin = rx_pin, 
        tx_pin = tx_pin 
    }
    setmetatable(b, self)
    self.__index = self
    return b
end

function DaliBus:send_cmd(addr, cb) 
    error("Not implemented")
end



function DaliBus:scan()
    for addr=0,64 do
        print("Scanning", addr)
    end
    return {
        code = 204
    }
end

function DaliBus:init()
    return {
        body = "This operation is not yet defined",
        code = 501,
    }
end


return DaliBus