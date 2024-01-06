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

---Lists all the resource paths that this device has registered
---@return table
local function list_resources_handler()
    local resourceList = cbor.encode_as_list{}
    for path, _val in pairs(coap.resources) do
        table.insert(resourceList, path)
    end

    -- Also add the patern paths - they should be easily distinguishable from literal paths.
    for path, _val in pairs(coap.pattern_resources) do
        table.insert(resourceList, path)
    end
    return coap.cbor_response(resourceList)
end



coap.resources = {
    restart= {
        post=restart_handler
    },
    info={
        get=info_handler
    },
    -- This matches the root path (no path options supplied)
    [""]= {
        get=list_resources_handler
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

function table.tostring(t)
    local sep = "{"
    local ret = ""

    for k,v in pairs(t) do
        local type = type(v)
        local sval

        if type == "table" then
            sval = table.tostring(v)
        else
            sval = string.format("%s", v)
        end
        ret = ret..sep..k.."="..sval
        sep = ","
    end
    return ret.."}"
end


---A simple COAP handler that looks up handlers in the coap.resources field.
---@param req any A request object with information on the CoAP request
---@return any what will be sent back to the caller.  May be nil, a string, or a table
coap.set_coap_handler(function(req)
    req.path_str = table.concat(req.path, "/")
    local methods = coap.lookup(req.path_str)
    if methods then
        local handler = methods[req.code]
        if handler then
            return handler(req)
        end
    end
    log:warn("No path matches", req.path_str)
    return coap.not_found()
end)