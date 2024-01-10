local log = Logger:get("router")

require("async")


local function restart_handler(req)
    -- We run the restart in a separate task after a small delay so that the coroutine can return
    start_async_task(function()
        log:warn("Restarting Device");
        await(Future:defer(250))
        system.restart()
    end)
    req.reply(coap.changed())
end


--- Makes a deep copy of a table, so that we can mess with it without messing with the original. 
local function copy_table(t, into)
    local c = into or {}
    for k, v in pairs(t) do
        if type(v) == 'table' then
            c[k] = copy_table(v)
        else
            c[k] = v
        end
    end
    return c
end


local function info_handler(req)

    local fw = system.firmware
    -- lazily give the cbor serialiser hints for how to serialise the binary sha256 field
    local system_meta = getmetatable(fw)
    if not system_meta then
        setmetatable(fw, { __valenc= {sha256='bstr'} })
    end

    local info = {
        esp_idf_ver=system.esp_idf_ver,
        uptime=os.clock(),
        mac_address=system.mac_address,
        reset_reason=system.reset_reason,
        firmware= fw
    }
    setmetatable(info, { __valenc={ mac_address='bstr' }})
    req.reply(coap.cbor_content(info));
end


---Lists all the resource paths that this device has registered
---@return table
local function list_resources_handler(req)
    local resourceList = copy_table(coap.resources)
    -- Also add the pattern resources
    copy_table(coap.pattern_resources, resourceList)

    -- before we return, change each of the objects in the copy from the handler/desc pair into just the description
    for path, methods in pairs(resourceList) do
        for method, operation in pairs(methods) do
            methods[method] = operation.desc or "No description"
        end
    end

    req.reply(coap.cbor_content(resourceList));
end


---Convenience function for encoding some data as cbor, then wrapping it in a CoAP response
function coap.cbor_content(content)
    return {
        code="content",
        format="cbor",
        payload=cbor.encode(content)
    }
end

-- All the possible COAP response codes - used to create helpers
local response_codes = {
    "emtpy",
    "created",
    "deleted",
    "valid",
    "changed",
    "content",
    "continue",

    "bad_request",
    "unauthorized",
    "bad_option",
    "forbidden",
    "not_found",
    "method_not_allowed",
    "not_accpetable",
    "request_incomplete",
    "precondition_failed",
    "request_too_large",
    "unsupported_format",

    "internal_error",
    "not_implemented",
    "bad_gateway",
    "service_unavailable",
    "gateway_timeout",
    "proxy_not_supported",
}

-- Make some convenience handlers.
for i,code in ipairs(response_codes) do
    coap[code] = function(p) return { code=code, payload=p } end
end


local function get_log_level(req)
    local tag = req.path_parameters[1]
    log:info("Getting log level for ", tag)
    req.reply(Logger:get(tag).level)
end

local function set_log_level(req)
    local tag = req.path_parameters[1]
    log:info("Setting log level for ", tag, "to", req.payload)
    Logger:get(tag).level = req.payload
    req.reply(coap.changed())
end


coap.resources = {
    restart= {
        post={
            handler=restart_handler,
            desc="restarts device"
        }
    },
    info={
        get={
            handler=info_handler,
            desc="fetches device info"
        }
    },
    -- This matches the root path (no path options supplied)
    [""]= {
        get={
            handler=list_resources_handler,
            desc="lists all resource handlers"
        }
    }
}
coap.pattern_resources = { 
    ["^log/([^/]+)$"] = {
        get={
            handler=get_log_level,
            desc="gets the log threshold"
        },
        put={
            handler=set_log_level,
            desc="sets the log threshold"
        }
    }
}


local function lookup(path)
    log:debug("looking for handler for path", path)
    local e = coap.resources[path]
    if e then
        return e
    else
        for pattern,methods in pairs(coap.pattern_resources) do
            local groups = {string.match(path, pattern)}
            if groups[1] then
                return methods, groups
            end
        end
    end
end


local function payload_len(pkt)
    if type(pkt) == 'table' and pkt.payload then
        return string.len(pkt.payload)
    else
        return -1
    end
end

---A simple COAP handler that looks up handlers in the coap.resources field.
---@param dgram table an object with a body string that represents the received datagram 
---@return any what will be sent back to the caller.  May be nil, a string, or a table
local sock = openthread:listen_udp(5683, function(self, dgram)
    local req = coap.decode(dgram.body);
    req.reply = function(resp)
        -- Wrap the respone, if it isn't already in the right format.
        if type(resp) ~= 'table' then
            resp = { payload=resp }
        end
        resp.type = "ack"
        -- Copy message_id and token from the request
        resp.token = req.token
        resp.message_id = req.message_id
        -- Infer a response code, if one has not yet been set
        if not resp.code then
            if req.code == 'get' then
                resp.code = 'content'
            elseif req.code == 'put' or req.code == 'post' then
                resp.code = 'changed'
            elseif req.code == 'delete' then
                resp.code = 'deleted'
            else
                error("can not infer response code")
            end
        end
        -- Log the request
        log:info(string.format("%s %s %d - %s %d", req.code, req.path_str, payload_len(req), resp.code, payload_len(resp)));
        -- Send a datagram with the encoded respopnse
        req.replied = true
        dgram:reply(coap.encode(resp))
    end

    req.path_str = table.concat(req.path, "/")
    local methods, groups = lookup(req.path_str)
    if methods then
        local operation = methods[req.code]
        if operation then
            req.path_parameters = groups
            log:debug("invoking operation", operation.desc)
            local success, res = pcall(operation.handler, req)
            if success then
                if not req.replied then
                    log:warn("Handler did not reply.  Message will not be responded to")
                end
                if res ~= nil then
                    log:warn("Handler returned a value, but we don't do anything with it")
                end
            else
                log:error(res, debug.traceback(res))
                req.reply(coap.internal_error(res))
            end
            return
        end
    end
    log:warn("No path matches", req.path_str)
    req.reply(coap.not_found())
end)

-- Return the socket in case somebody wants to close it later.
return sock