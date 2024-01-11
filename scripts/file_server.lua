require "router"

local log = Logger:get("router")


local config = {}
---The maximum number of file items that will be returned in any one call to handle_list
--- In other words, its the page size. 
config.list_max_items = 20


--- Calculates the file size of an open file_pointer
--- Really, LUA should offer this up already, but we solve it by using seek. 
local function file_size(fp)
    local pos = fp:seek("end", 0)
    return pos
end

local function get_file_etag(fname)
    local fp = io.open(fname, "rb")
    if not fp then
        error("File %s does not exist", fname)
    end
    local hasher = digest:md5()

    local block = fp:read(4096)
    while block do
        hasher:update(block)
        block = fp:read(4096)
    end
    fp:close()
    return hasher:digest()
end

---Checks to see if a table contains an item
---@param table any
---@param value any
---@return boolean
local function tableContains(table, value)
    for i,v in ipairs(table) do
      if (v == value) then
        return true
      end
    end
    return false
end

---Does a block wise fetch of a file from the filesystem
---@param req any The CoAP Request
---@return any The One block of content from the file or an error code
local function handle_fs_read(req)
    local block2 = req.block2 or {
        id = 0,
        size = 1024,
    }
    local path = "/"..req.path_str

    -- Allow for etag matching
    if req.if_match then
        local etag = get_file_etag(path)
        if not tableContains(req.if_match, etag) then
            -- The etag does not match any of the values supplied
            log:debug("Etag does not match supplied if-match - returning 'valid' response")
            return req.reply(coap.valid())
        end
    end

    local f = io.open(path, "rb")
    if f then
        local sz = file_size(f)
        -- log:debug("File size is", sz)
        local num_blocks = math.ceil(sz/block2.size)

        if block2.id > num_blocks then
            log:debug("attempt to fetch block beyond end of file")
            return req.reply(coap.bad_option("BlockID2 index beyond block count of file"))
        end

        local offset = block2.id * block2.size
        f:seek("set", offset)
        -- log:debug("Reading", block2.size, "from", offset)
        local d = f:read(block2.size)
        -- log:debug("Read", string.len(d))
        f:close()
        req.reply{
            payload= d,
            block2={ id=block2.id, size=block2.size, more=block2.id < num_blocks-1}
        }
    else
        req.reply(coap.not_found())
    end
end


local function handle_fs_write(req)
    local block1 = req.block1 or {
        id = 0,
        size = 1024,
        more = false
    }
    log:info("Writing file", req.path_str, "Block", block1.id, block1.size, block1.more, #req.payload)

    local path = "/"..req.path_str
    if req.if_none_match and io.exists(path) then
        log:debug("File exists, but if-none-match was specified")
        return req.reply(coap.precondition_failed())
    end

    if req.if_match then
        local etag = get_file_etag(path)
        if not tableContains(req.if_match, etag) then
            -- The etag does not match any of the values supplied
            log:debug("Etag does not match supplied if-match - Not overwriting")
            return req.reply(coap.precondition_failed())
        end
    end

    local mode
    if block1.id == 0 then
        mode = "wb"
    else
        mode = "ab"
    end
    local fp = io.open(path, mode)
    if not fp then
        return req.reply(cbor.internal_error("Could not open file for writing"))
    end
    local pos = fp:seek("end")
    local expectedPos = block1.id * block1.size
    
    if pos > expectedPos then
        -- This is probably caused by a re-transmission of a previous request - Accept it with equinmity.
        log:warn("Ignoring re-transmitted block")
    elseif pos > expectedPos then
        log:warn("Expected file to be", expectedPos, "but it was at", pos)
        fp:close()
        return req.reply(cbor.not_accpetable("Block has already been written"))
    else
        fp:write(req.payload)
        fp:close();
    end

    local resp = {
        block1=req.block1
    }

    if req.block1 and req.block1.more then
        resp.code = "continue"
    end
    return req.reply(resp);
end

local function handle_fs_delete(req)
    local fname = "/"..req.path_str

    if req.if_match then
        local etag = get_file_etag(fname)
        if not tableContains(req.if_match, etag) then
            -- The etag does not match any of the values supplied
            log:debug("Etag does not match supplied if-match - Not overwriting")
            return req.reply(coap.precondition_failed())
        end
    end

    local ret, msg = os.remove(fname);
    if not ret then
        return req.reply(coap.not_found(msg));
    end
end


local function handle_list(req)
    local dirname = "/"..req.path_str
    log:info("Listing directory", dirname);

    -- If there's a query, we will start our list from that value (inclusive)
    local startfrom = ""
    if req.query and #req.query > 0 then
        startfrom = req.query[1]
    end
    local file_list = {}
    -- Values in the table are binary strings, not utf8 - we have to give the encoder a hint
    setmetatable(file_list, { __valenc= "bstr" })

    local filenames = io.readdir(dirname)
    -- we sort them so that we can paginate
    table.sort(filenames)

    local nextval = nil
    local num_added = 0
    for i,fname in ipairs(filenames) do
        if fname >= startfrom then
            if num_added > config.list_max_items then
                -- There are too many entries, and we run the risk of overflowing our packet size. Send a continuation token and return
                nextval = fname
                break
            end
            local fullname = dirname.."/"..fname
            file_list[fname] = get_file_etag(fullname)
            num_added = num_added + 1
        end
    end

    return req.reply(coap.cbor_content{
        files=file_list,
        next=nextval
    })
end

-- Add some handlers to the coap server
coap.resources[{"fs"}]={
    get={
        handler=handle_list,
        desc="lists all files on the device's filesystem"
    }
}

coap.resources[{"fs", "*"}]={
    get={
        handler=handle_fs_read,
        desc="reads a file"
    },
    put={
        handler=handle_fs_write,
        desc="writes a file"
    },
    delete={
        handler=handle_fs_delete,
        desc="deletes a file"
    }
}

-- Return the config so that it can be customised
return config