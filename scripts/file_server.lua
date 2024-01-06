require "router"

local log = Logger:new("router")


--- Calculates the file size of an open file_pointer
--- Really, LUA should offer this up already, but we solve it by using seek. 
local function file_size(fp)
    local pos = fp:seek("end", 0)
    return pos
end


---Does a block wise fetch of a file from the filesystem
---@param req any The CoAP Request
---@return CoapResponse The One block of content from the file or an error code
local function handle_fs_read(req)
    local block2 = req.block2 or {
        id = 0,
        size = 1024,
    }
    log:info("Fetching", req.path_str, "block", block2.id, "@", block2.size)

    local f = io.open("/"..req.path_str, "rb")
    if f then
        local sz = file_size(f)
        -- log:debug("File size is", sz)
        local num_blocks = math.ceil(sz/block2.size)

        if block2.id > num_blocks then
            log:debug("attempt to fetch block beyond end of file")
            return coap.bad_option("BlockID2 index beyond block count of file")
        end

        local offset = block2.id * block2.size
        f:seek("set", offset)
        -- log:debug("Reading", block2.size, "from", offset)
        local d = f:read(block2.size)
        -- log:debug("Read", string.len(d))
        f:close()
        return coap.response{
            payload= d,
            block2=coap.block_opt(block2.id, block2.size, block2.id < num_blocks-1)
        }
    else
        return coap.not_found()
    end
end


local function handle_fs_write(req)
    local block1 = req.block1 or {
        id = 0,
        size = 1024,
        more = false
    }
    log:info("Writing file", req.path_str, "Block", block1.id, block1.size, block1.more, #req.payload)
    local mode
    if block1.id == 0 then
        mode = "wb"
    else
        mode = "ab"
    end
    local fp = io.open("/"..req.path_str, mode)
    if not fp then
        return coap.internal_error("Could not open file for writing")
    end
    local pos = fp:seek("end")
    local expectedPos = block1.id * block1.size
    
    if pos > expectedPos then
        -- This is probably caused by a re-transmission of a previous request - Accept it with equinmity.
        log:warn("Ignoring re-transmitted block")
    elseif pos > expectedPos then
        log:warn("Expected file to be", expectedPos, "but it was at", pos)
        fp:close()
        return coap.not_accpetable("Block has already been written")
    else
        fp:write(req.payload)
        fp:close();
    end

    local resp =  coap.response{
        block1=coap.block_opt(block1.id, block1.size, block1.more)
    }
    if block1.more then
        resp.code = coap.CODE_CONTINUE
    end
    return resp;
end

local function handle_fs_delete(req)
    local fname = "/"..req.path_str
    if not os.remove(fname) then
        return coap.not_found();
    end
end


local function handle_list(req)
    local dirname = "/"..req.path_str
    log:info("Listing directory", dirname);

    local file_list = {}
    -- Values in the table are binary strings, not utf8 - we have to give the encoder a hint
    setmetatable(file_list, { __valenc= "bstr" })

    local filenames = io.readdir(dirname)
    -- we sort them so that we can paginate
    table.sort(filenames)

    local nextval = nil

    for i,fname in ipairs(filenames) do
        local fullname = dirname.."/"..fname
        local fp = io.open(fullname, "rb")
        if fp == nil then
            error("Could not open file "..fullname)
        end
        local hasher = digest:md5()
        hasher:update(fp:read("a"))
        local digest = hasher:digest()
        file_list[fname] = digest
        fp:close()
    end

    return coap.cbor_response{
        files=file_list,
        next=nextval
    }
end

coap.resources["fs"]={
    get=handle_list,
}

coap.pattern_resources["^fs/"]={
    get=handle_fs_read,
    put=handle_fs_write,
    delete=handle_fs_delete,
}

