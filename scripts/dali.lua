

Dali = {}

local log = Logger:new("dali")

---transforms a logical gear address into the value transmitted for DALI commands.
---@param logical_address integer between 0 and 63 inclusive indicating the logical address of the gear.
---@return integer the value once shifted
function Dali:gear_address(logical_address)
    if logical_address < 0 or logical_address > 63 then
        error("Address must be between 0 and 63 inclusive, but %d was passed", logical_address)
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
        self:goto_last_active_level(addr)
    else
        self:off(addr)
    end
end



function Dali:register_device(addr)
    self.registered_gear_addresses[addr] = true
end


function Dali:scan() 
    log:info("Doing initial scan of Dali devices")
    for addr=0,63 do
        local res = self:query_actual(addr)
        if res >= 0 then
            self:register_device(addr)
        end
    end
end


---A  wrapper to extract common parameters out, then pass that through to a more specific handler.
function Dali:parse_addr(req, fn)
    local logical_addr = tonumber(req.path[2])
    if not logical_addr then
        return coap.bad_request()
    end

    if not self.registered_gear_addresses[logical_addr] then
        return coap.not_found()
    end
    local response = fn(self, req, logical_addr)
    if getmetatable(respone) ~= coap then
        return coap.cbor_response(response)
    else
        return response
    end
end


---Gets attributes of the dali gear at the supplied address
---@param req any
---@param logical_addr any
---@return unknown
function Dali:handle_get(req, logical_addr)
    local level = self:query_actual(logical_addr)
    log:info("level of device", logical_addr, "is", level)
    return {
        level=level -- This will be CBOR encoded
    }
end


function Dali:handle_post(req, logical_addr)
    log:info("Posted to device", logical_addr);
    self:toggle(logical_addr);
end


---Lists the logical addresses of all discovered dali devices.
---@param req any
---@return unknown
function Dali:handle_list_devices(req)
    local devices = cbor.encode_as_list{}
    for id,val in ipairs(self.registered_gear_addresses) do
        table.insert(devices, id)
    end

    return coap.cbor_response(devices)
end




--- Starts a DALI driver, and creates COAP resources to represent devices on the bus.
---@param tx integer The Transmit pin to use
---@param rx integer the Receive pin to use
---@return table
function Dali:new(tx, rx)
    local d = {
        bus = DaliBus:new(tx,rx),
        registered_gear_addresses={}
    }
    setmetatable(d, self)
    self.__index = self

    coap.resources["dali"]={
        get={
            handler=function(req)
                return d:handle_list_devices(req)
            end
        }
    }

    coap.pattern_resources["^dali/%d%d?"]={
        get={
            handler=function(req)
                return d:parse_addr(req, Dali.handle_get)
            end,
            desc="Fetches attributes of a dali device"
        },
        post={
            handler=function(req)
                return d:parse_addr(req, Dali.handle_post)
            end,
            desc="Modifies a dali device"
        }
    }

    system.start_task(function()
        d:scan()
    end)
    return d
end

return Dali