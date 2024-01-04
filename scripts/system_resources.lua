

local function file_size(fp)
    local pos = fp:seek("end", 0)
    return pos
end


---Does a block wise fetch of a file from the filesystem
---@param req any The CoAP Request
---@param res any A table that will be used to house response parameters, if this function chooses to set them
---@return string|nil The One block of content from the file or nill if there was some sort of error - In which case res.code will have been set.
local function handle_fs_read(req, res)
    local block2 = req.block2 or {
        id = 0,
        size = 1024,
        size_ex = 6
    }
    log.debug("Block size is", block2.id, block2.size, block2.size_ex)



    local f = io.open("/"..req.path_str, "rb")
    if f then
        local sz = file_size(f)
        log.debug("File size is", sz)
        local num_blocks = math.ceil(sz/block2.size)

        if block2.id > num_blocks then
            log.debug("attempt to fetch block beyond end of file")
            res.code = coap.CODE_BAD_OPTION
            return
        end

        local offset = block2.id * block2.size
        f:seek("set", offset)
        log.debug("Reading", block2.size, "from", offset)
        res.block2 = {
            id=block2.id,
            size_ex=block2.size_ex,
            more=block2.id < num_blocks
        }
        local d = f:read(block2.size)
        log.debug("Read", string.len(d))
        f:close()
        return d
    else
        log.warn("Open didn't work")
        -- Treat it as a not found.
    end
    res.code = coap.CODE_NOT_FOUND
end




local function restart_handler()
    -- We run the restart in a separate task after a small delay so that the coroutine can return
    system.start_task(function()
        log.warn("COAP request to restart");
        system.await({ timeout = 250 })
        system.restart()
    end)
end

local function info_handler()
    return {
        ver="dev",
        uptime=os.clock()
    }
end

---Lists all the resource paths that this device has registered
---@return table
local function list_resources_handler()
    local res = {}
    for path, _val in pairs(coap.resources) do
        table.insert(res, path)
    end

    -- Also add the patern paths - they should be easily distinguishable from literal paths.
    for path, _val in pairs(coap.pattern_resources) do
        table.insert(res, path)
    end
    return res
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
coap.pattern_resources = {
    ["^fs/"]={
        get=handle_fs_read
    },

}


function coap.lookup(path)
    log.debug("looking for handler for path", path)
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
    log.info("Received CoAP request")

    req.path_str = table.concat(req.path, "/")
    local methods = coap.lookup(req.path_str)
    if methods then
        local handler = methods[req.code]

        if handler then
            return handler(req, req.res)
        end
    end
    log.info("No path matches ", req.path_str)
    req.res.code = coap.CODE_NOT_FOUND
end)