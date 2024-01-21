Dali = {}
require("async")

local log = Logger:get("dali")


---transforms a logical gear address into the value transmitted for DALI commands.
---@param logical_address integer between 0 and 63 inclusive indicating the logical address of the gear.
---@return integer the value once shifted
function Dali:gear_address(logical_address)
    if logical_address < 0 or logical_address > 63 then
        error("Address must be between 0 and 63 inclusive, but %d was passed", logical_address)
    end
    return logical_address << 9;
end

--- Glues a dali transmit callback to the async/await system, returning the result.
function Dali:await_cmd(cmd)
    -- Only one command can be waiting at a time
    local f = Future:new()
    self.bus:transmit(cmd, Future.set, f)
    return await(f)
end

---Queries a device to determine what its current level is
---@param addr integer The address between 0 and 63 inclusive to modify
---@return integer between 0 and 254 representing the current brigtness on a logartithmic scale.
function Dali:query_actual(addr)
    return self:await_cmd(self:gear_address(addr) | 0x1a0)
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

function Dali:goto_last_active_level(addr)
    self.bus:transmit(self:gear_address(addr) | 0x10a)
end

function Dali:off(addr)
    self.bus:transmit(self:gear_address(addr) | 0x100)
end

function Dali:register_device(addr)
    self.registered_gear_addresses[addr] = true
end

function Dali:scan()
    log:info("Doing initial scan of Dali devices")
    for addr = 0, 63 do
        local res = self:query_actual(addr)
        if res >= 0 then
            log:info("Found gear at address", addr)
            self:register_device(addr)
        end
    end
    log:info("Completed scan")
end

function Dali:parse_addr(req)
    local logical_addr = tonumber(req.path[2])
    if not logical_addr then
        req.reply { code = "bad_request" }
        return nil
    end

    if not self.registered_gear_addresses[logical_addr] then
        req.reply { code = "not_found" }
        return nil
    end

    return logical_addr
end

function Dali:process_action(req, addr)
    local action, description, times, dtr, response = table.unpack(self.actions[req.path[3]])

    if dtr then
        log:warn("DTR based actions not implemented yet")
    end

    if response then
        local ret = self:await_cmd(self:gear_address(addr) | action)
        req.reply { code = "content", format = "cbor", payload = cbor.encode(ret) }
    else
        local cmd = self:gear_address(addr) | action
        log:info(string.format("Tranmsitting command 0x%04x", cmd));
        self.bus:transmit(cmd)
        if times then
            -- Send it again!
            self.bus:transmit(cmd)
        end
        req.reply { code = "changed" }
    end
    -- Do this at the end for latency reasons
    log:info("Executed action", description, "on address", addr);
end

--- Starts a DALI driver, and creates COAP resources to represent devices on the bus.
---@param tx integer The Transmit pin to use
---@param rx integer the Receive pin to use
---@return table
function Dali:new(tx, rx)
    local d = {
        bus = DaliBus:new(tx, rx),
        registered_gear_addresses = {},
        actions = {
            off = { 0x100, "turns device off" },
            up = { 0x101, "brightens light" },
            down = { 0x102, "dims light" },
            step_up = { 0x103, "steps up brightness" },
            step_down = { 0x104, "steps down brightness" },
            recall_max_level = { 0x105, "takes device to its maximum configured brightness" },
            recall_min_level = { 0x106, "takes device to its maximum configured brightness" },
            step_down_and_off = { 0x107, "Steps the device's brightness down, turning it off if it reaches miniumum value" },
            on_and_step_up = { 0x108, "Steps the device's brightness up, turning it on first, if necessary" },
            enable_dapc_sequence = { 0x109, "Dunno" },
            last_active = { 0x10a, "Sets brightness to its last active level" },
            continuous_up = { 0x10b, "Tells device to get brighter until another command is sent" },
            continuous_down = { 0x10c, "Tells device to get dimmer until another command is sent" },
            reset = { 0x120, "Resets the device", 2 },
            store_actual_level_in_dtr0 = { 0x121, "Returns the current level", 2, nil, 0 },
            save_persistent_variables = { 0x122, "Saves set values to flash on the device", 2 },
            reset_memory_bank = { 0x124, "Resets a memory bank", 2, 0 },
            identify_device = { 0x125, "Identifies the device (makes in blink)", 2 },
            enable_write_memory = { 0x181, "Enable writing to memory banks", 2 },
        },
        queries = {
            status = { 0x190, "Queries device status" },
            control_gear_present = { 0x191, "Checks to see if control gear is present" },
            lamp_failure = { 0x192, "Queries for lamp failure" },
            lamp_power_on = { 0x193, "Checks to see if power is on" },
            limit_error = { 0x1944, "See if there has been an attempt to drive device past its limit" },
            reset_state = { 0x195, "Query why it was reset" },
            missing_short_address = { 0x196, "Ask if it is missing its short address" },
            version_number = { 0x197, "Query its version number" },
            dtr0 = { 0x198, "Get the content of DTR0" },
            device_type = { 0x199, "Get Device Type" },
            physical_minimum = { 0x19a, "Query the phsical minimum" },
            power_failure = { 0x19b, "Query if there is a power failure" },
            dtr1 = { 0x19c, "Get the content of DTR1" },
            dtr2 = { 0x19d, "Get the content of DTR2" },
            operating_mode = { 0x19e, "Get the operating mode" },
            light_source_type = { 0x19f, "Get the light source type" },
            actual_level = { 0x1a0, "Get the actual level of the light" },
            maufacturer_specific_mode = { 0x16, "Get the manufacturer specific mode" },
            next_device_type = { 0x17, "Fetch the next device type" },
            control_gear_failure = { 0x1a, "Queries if there has been control gear failure" },
            groups_zero_to_seven = { 0x1c0 },
            groups_eight_to_fifteen = { 0x1c1 },
            random_address_h = { 0x1c2 },
            random_address_m = { 0x1c3 },
            random_address_l = { 0x1c4 },
            read_memory_location = { 0x1c5 }, -- This should be treated specially
        },
        group_actions = {
            goto_scene = { 0x110 },
            set_scene = { 0x140, 2, 0 },
            remove_from_scene = { 0x150, 2, 0 },
            scene_level = { 0x1b0 },

            add_to_group = { 0x160, 2, 0 },
            remove_from_group = { 0x170, 2, 0 },
        },

        rw_attributes = {
            max_level = { 0x12a, 0x1a1 },
            min_level = { 0x12b, 0x1a2 },
            power_on_level = { 0x12d, 0x1a3 },
            system_failure_level = { 0x12c, 0x1a4 },
            short_address = { -1, 0x1a4 }, -- Setting Short address is a whole thing
            fade_time = { 0x12e, 0xa5 },   -- Note these two only have one setter?
            fade_rate = { 0x12f, 0xa5 },
            extended_fade_time = { 0x130, 0x1a8 },
            operating_mode = { 0x123, 0x19e },

        }
    }
    setmetatable(d, self)
    self.__index = self


    coap.resources[{ "dali" }] = {
        get = {
            desc = 'Lists all dali devices',
            handler = function(req)
                local devices = cbor.encode_as_list {}
                for id, val in pairs(d.registered_gear_addresses) do
                    table.insert(devices, id)
                end
                table.sort(devices)
                req.reply { code = "content", format = "cbor", payload = cbor.encode(devices) }
            end
        }
    }

    coap.resources[{ "dali", "^%d%d?$" }] = {
        get = {
            handler = function(req)
                local addr = d:parse_addr(req)
                if addr then
                    start_async_task(function()
                        local level = d:query_actual(addr)
                        req.reply {
                            code = "content",
                            format = "cbor",
                            payload = cbor.encode { level = level }
                        }
                    end)
                end
            end,
            desc = "Fetches attributes of a dali device"
        },
    }


    local action_handler = function(req)
        local addr = d:parse_addr(req)
        if addr then
            start_async_task(function()
                d:process_action(req, addr)
            end)
        else
            req.reply { code = "not_found" }
        end
    end


    --- There are a lot of Dali commands that you can send to gear. Instead of writing a function for each one,
    --- we specify a set of paramters here, and the default handler
    for name, action in pairs(d.actions) do
        log:info("Setting handler for " .. name)
        coap.resources[{ "dali", "^%d%d?$", name }] = {
            post = {
                handler = action_handler,
                desc = action[2]
            },
        }
    end

    coap.resources[{ "dali", "^%d%d?$", "toggle" }] = {
        post = {
            handler = function(req)
                local addr = d:parse_addr(req)
                if addr then
                    start_async_task(function()
                        d:toggle(addr)
                        req.reply { code = "changed" }
                    end)
                else
                    req.reply { code = "not_found" }
                end
            end,
            desc = "Toggles light"
        },
    }


    start_async_task(function()
        d:scan()
    end)
    return d
end

return Dali
