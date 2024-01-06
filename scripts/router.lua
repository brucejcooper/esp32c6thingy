local log = Logger:new("router")


local function restart_handler()
    -- We run the restart in a separate task after a small delay so that the coroutine can return
    system.start_task(function()
        log:warn("COAP request to restart");
        system.await({ timeout = 250 })
        system.restart()
    end)
end

local function info_handler()
    return coap.cbor_response{
        ver="dev",
        uptime=os.clock()
    }
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
local function list_resources_handler()
    local resourceList = copy_table(coap.resources)
    -- Also add the pattern resources
    copy_table(coap.pattern_resources, resourceList)

    -- before we return, change each of the objects in the copy from the handler/desc pair into just the description
    for path, methods in pairs(resourceList) do
        for method, operation in pairs(methods) do
            methods[method] = operation.desc or "No description"
        end
    end

    return coap.cbor_response(resourceList)
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


function coap.lookup(path)
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



---A simple COAP handler that looks up handlers in the coap.resources field.
---@param req any A request object with information on the CoAP request
---@return any what will be sent back to the caller.  May be nil, a string, or a table
coap.set_coap_handler(function(req)
    req.path_str = table.concat(req.path, "/")
    local methods = coap.lookup(req.path_str)
    if methods then
        local operation = methods[req.code]
        if operation then
            log:debug("invoking operation", operation.desc)
            return operation.handler(req)
        end
    end
    log:warn("No path matches", req.path_str)
    return coap.not_found()
end)