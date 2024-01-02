

Dali = {}

function Dali:gear_address(logical_address)
    if logical_address < 0 or logical_address > 63 then
        error("Address must be between 0 and 63 inclusive")
    end
    return logical_address << 9;
end

function Dali:query_actual(addr) 
    return self.bus:transmit(self:gear_address(addr) | 0x1a0)
end

function Dali:off(addr) 
    return self.bus:transmit(self:gear_address(addr) | 0x100)
end

function Dali:goto_last_active_level(addr) 
    return self.bus:transmit(self:gear_address(addr) | 0x10a)
end



function Dali:serve_device(msg)
    if msg.code == coap.CODE_GET then
        local addr = tonumber(msg.path[#msg.path])
        local level = self:query_actual(addr)
        log.info("level of device", addr,"is",level)
        return {
            level=level -- This will be CBOR encoded
        }
    else
        msg.response_code = coap.CODE_METHOD_NOT_ALLOWED
    end
end


function Dali:register_device(addr)
    log.info("Address", addr)
    coap.resource(string.format("dali/%d", addr), function(msg)
        return self:serve_device(msg)
    end)
end


function Dali:scan() 
    log.info("Doing initial scan of Dali devices")
    for addr=0,63 do
        local res = self:query_actual(addr)
        if res >= 0 then
            self:register_device(addr)
        end
    end

end


--- Starts a DALI driver, and creates COAP resources to represent devices on the bus.
---@param tx integer The Transmit pin to use
---@param rx integer the Receive pin to use
---@return table
function Dali:new(tx, rx)
    local d = {
        bus = DaliBus:new(tx,rx)
    }
    setmetatable(d, self)
    self.__index = self

    system.start_task(function()
        d:scan()
    end)
    return d
end

return Dali