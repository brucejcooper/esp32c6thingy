

Dali = {}

---transforms a logical gear address into the value transmitted for DALI commands.
---@param logical_address integer between 0 and 63 inclusive indicating the logical address of the gear.
---@return integer the value once shifted
function Dali:gear_address(logical_address)
    if logical_address < 0 or logical_address > 63 then
        error("Address must be between 0 and 63 inclusive")
    end
    return logical_address << 9;
end

---Queries a device to determine what its current level is 
---@param addr integer The address between 0 and 63 inclusive to modify
---@return integer between 0 and 254 representing the current brigtness on a logartithmic scale.
function Dali:query_actual(addr) 
    return self.bus:transmit(self:gear_address(addr) | 0x1a0)
end

---Turns a device off.
---@param addr integer The address between 0 and 63 inclusive to modify
function Dali:off(addr) 
    return self.bus:transmit(self:gear_address(addr) | 0x100)
end

---Turns the device on, and sends it to its last know active level.
---@param addr integer The address between 0 and 63 inclusive to modify
function Dali:goto_last_active_level(addr) 
    self.bus:transmit(self:gear_address(addr) | 0x10a)
end

---Toggles the specified address.  If it was off turn it to its last active level.  If it was on, turn it off.
---@param addr integer The address between 0 and 63 inclusive to toggle
function Dali:toggle(addr) 
    if self:query_actual(addr) == 0 then
        self:goto_last_active_level()
    else
        self:off()
    end
end


function Dali:serve_device(msg)
    local logical_addr = tonumber(msg.path[#msg.path])
    local physical_addr = self:gear_address(logical_addr)
    
    if msg.code == coap.CODE_GET then
        local level = self:query_actual(physical_addr)
        log.info("level of device", logical_addr, "is", level)
        return {
            level=level -- This will be CBOR encoded
        }
    elseif msg.code == coap.CODE_PUT then
        log.info("Posted to device", logical_addr);
        self:toggle(physical_addr);
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