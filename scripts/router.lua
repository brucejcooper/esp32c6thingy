local log = Logger:new("router")


local function restart_handler(req)
    -- We run the restart in a separate task after a small delay so that the coroutine can return
    system.start_task(function()
        log:warn("COAP request to restart");
        system.await({ timeout = 250 })
        system.restart()
    end)
    req.reply(coap.changed())
end

local function info_handler(req)
    req.reply(coap.cbor_content{
        ver="dev",
        uptime=os.clock()
    });
end

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
            desc="fetches information about the device"
        }
    },
    -- This matches the root path (no path options supplied)
    [""]= {
        get={
            handler=list_resources_handler,
            desc="lists all resource handlers that the device has"
        }
    }
}
coap.pattern_resources = { }


local function lookup(path)
    log:debug("looking for handler for path", path)
    local e = coap.resources[path]
    if e then
        return e
    else
        for pattern,methods in pairs(coap.pattern_resources) do
            if (string.match(path, pattern)) then
                return methods
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
    local methods = lookup(req.path_str)
    if methods then
        local operation = methods[req.code]
        if operation then
            log:info("invoking operation", operation.desc)
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