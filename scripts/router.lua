local log = Logger:get("router")

require("async")
require("helpers")


coap.resources = {}



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


function coap.make_code(major,minor)
    return (major << 5) + minor
end

coap.codes = {
    [coap.make_code(0, 0)]= 'empty',
    [coap.make_code(0,1)] = "get",
    [coap.make_code(0,2)] = "post",
    [coap.make_code(0,3)] = "put",
    [coap.make_code(0,4)] = "delete",
    
    [coap.make_code(2,0)] = "response_min",
    [coap.make_code(2,1)] = "created",
    [coap.make_code(2,2)] = "deleted",
    [coap.make_code(2,3)] = "valid",
    [coap.make_code(2,4)] = "changed",
    [coap.make_code(2,5)] = "content",
    [coap.make_code(2,31)] = "continue",
    
    [coap.make_code(4,0)] = "bad_request",
    [coap.make_code(4,1)] = "unauthorized",
    [coap.make_code(4,2)] = "bad_option",
    [coap.make_code(4,3)] = "forbidden",
    [coap.make_code(4,4)] = "not_found",
    [coap.make_code(4,5)] = "method_not_allowed",
    [coap.make_code(4,6)] = "not_accpetable",
    [coap.make_code(4,8)] = "request_incomplete",
    [coap.make_code(4,12)] = "precondition_failed",
    [coap.make_code(4,13)] = "request_too_large",
    [coap.make_code(4,15)] = "unsupported_format",
    
    [coap.make_code(5,0)] = "internal_error",
    [coap.make_code(5,1)] = "not_implemented",
    [coap.make_code(5,2)] = "bad_gateway",
    [coap.make_code(5,3)] = "service_unavailable",
    [coap.make_code(5,4)] = "gateway_timeout",
    [coap.make_code(5,5)] = "proxy_not_supported",
}

coap.types = {
    "confirmable",
    "non-confirmable",
    "ack",
    "reset"
}


local function parse_opt_singleton(packet, opt_name, val)
    if packet[opt_name] then
        error(string.format("singleton option %s appears more than once", opt_name))
    end
    packet[opt_name] = val
end

local function parse_opt_many(packet, opt_name, val)
    local list = packet[opt_name]
    if not list then
        list = {}
        packet[opt_name] = list
    end
    table.insert(list, val)
end


local function bstr_to_int(bstr)
    local val = 0
    for i,v in ipairs({string.byte(bstr, 1, string.len(bstr))}) do
        val = val << 8 | v
    end
    return val
end


local function parse_opt_block(packet, opt_name, val)
    if packet[opt_name] then
        error(string.format("singleton option %s appears more than once", opt_name))
    end

    local ival = bstr_to_int(val)
    local more = false
    if ival & 0x08 then
        more = true
    end
    packet[opt_name] = {
        id=ival >> 4,
        size=1 << (4 + (ival & 0x07)),
        more=more
    }
end


coap.options = {
    [1]={ sval="if_match", parse=parse_opt_many},
    [3]={ sval="uri_host", parse=parse_opt_many},
    [4]={ sval="e_tag", parse=parse_opt_singleton},
    [5]={ sval="if_none_match", parse=parse_opt_singleton},
    [6]={ sval="observe", parse=parse_opt_many},
    [7]={ sval="uri_port", parse=parse_opt_many},
    [8]={ sval="location_path", parse=parse_opt_many},
    [11]={ sval="path", parse=parse_opt_many},
    [12]={ sval="format", parse=parse_opt_singleton},
    [14]={ sval="max_age", parse=parse_opt_many},
    [15]={ sval="query", parse=parse_opt_many},
    [17]={ sval="accept", parse=parse_opt_many},
    [20]={ sval="location_query", parse=parse_opt_many},
    [23]={ sval="block2", parse=parse_opt_block},
    [27]={ sval="block1", parse=parse_opt_block},
    [28]={ sval="size2", parse=parse_opt_many},
    [35]={ sval="proxy_uri", parse=parse_opt_many},
    [39]={ sval="proxy_scheme", parse=parse_opt_many},
    [60]={ sval="size1", parse=parse_opt_many},
}
-- Also put in reverse mapping.  Make a copy first so that the iterator doesn't get discombobulated
for opt_n, opt_meta in pairs(Helpers.assign(coap.options)) do
    coap.options[opt_meta.sval] = { id=opt_n }
end



local CoAPReader = {}
CoAPReader.__index = CoAPReader
function CoAPReader:new(b)
    local r = {
        buf= b,
        pos=1,
        last_opt = 0
    }
    setmetatable(r, self)
    return r
end

function CoAPReader:u8()
    assert(type(self) == 'table')
    local b = string.byte(self.buf, self.pos)
    self.pos = self.pos + 1
    return b
end

function CoAPReader:bytes(n)
    local b = string.sub(self.buf, self.pos, self.pos+n-1)
    self.pos = self.pos + n
    return b
end

function CoAPReader:u16()
    local b1, b2 = string.byte(self.buf, self.pos, self.pos+1)
    self.pos = self.pos + 2
    return b1 << 8 | b2;
end

function CoAPReader:opt()
    local b = self:u8()
    if b == 0xFF then 
        self.payload_marker_parsed = true
        return nil, nil
    end
    if b == nil then
        -- We just ran out of bytes
        return nil, nil
    end
    local delta = b >> 4
    local sz = (b & 0x0F)

    if delta == 15 or sz == 15 then
        error("invalid coap delta/size")
    end

    if delta == 13 then
        delta = 13 + self:u8()
    elseif delta == 14 then
        delta = 269 + self:u16()
    end

    if sz == 13 then
        sz = 13 + self:u8()
    elseif sz == 14 then
        sz = 269 + self:u16()
    end
    self.last_opt = self.last_opt + delta
    local opt_meta = coap.options[self.last_opt]
    if not opt_meta then
        error("Unknown option type")
    end
    if sz == 0 then
        return opt_meta
    else
        return opt_meta, self:bytes(sz)
    end
end

function CoAPReader:remainder()
    return string.sub(self.buf, self.pos)
end



function coap.parse_packet(buf)

    local r = CoAPReader:new(buf)

    local firstbyte = r:u8()
    local version=firstbyte >> 6
    assert(version == 1, "Invalid CoAP version")
    local code_num = r:u8()
    local code = coap.codes[code_num]
    assert(code, "Invalid code")

    local token_len = firstbyte & 0x0F;
    log:debug("Parsing CoAP request first byte is", firstbyte, "token length", token_len, "code is", code);
    local packet = {
        type=coap.types[(firstbyte >>4) & 0x03];
        code=code,
        message_id=r:u16(),
        token=r:bytes(token_len),
        path={},
    }
    local opt, opt_data = r:opt()
    while opt do
        log:debug("Processing option ", opt.sval, opt_data)
        opt.parse(packet, opt.sval, opt_data)
        opt, opt_data = r:opt()
    end

    if r.payload_marker_parsed then
        packet.payload = r:remainder()
    end
    return packet
end



local function paths_match(path_seq, route_seq)

    for i, cand_e in ipairs(route_seq) do
        local path_e = path_seq[i]
        if not path_e then
            return false
        end
        log:debug("comparing candidate element", cand_e, "to path", path_e)
        if cand_e == "**" then
            return true
        elseif not (cand_e == "*" or (string.match(cand_e, "^^") and string.match(path_e, cand_e)) or cand_e == path_e) then
            return false
        end
    end
    -- Lengths must match (except for ** which was handled in the loop)
    return #path_seq == #route_seq
end

local function lookup(path)
    log:debug("looking for handler for path", path)
    for route, methods in pairs(coap.resources) do
        log:debug("Testing path", route, "against request", path)
        if paths_match(path, route) then
            log:debug("Path matches")
            return methods
        else
            log:debug("Path does not match")
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
    local req = coap.parse_packet(dgram.body)
    -- log:debug("DGRAM", Helpers.hexlify(dgram.body), "packet", req.block2)

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
    local methods = lookup(req.path)
    if methods then
        local operation = methods[req.code]
        if operation then
            log:debug("invoking operation", operation.desc)
            local success, res = xpcall(operation.handler, function(err)
                -- Send the traceback along with the error
                return debug.traceback(err, 2)
            end, req)
            if success then
                if not req.replied then
                    log:warn("Handler did not reply.  Message will not be responded to")
                end
                if res ~= nil then
                    log:warn("Handler returned a value, but we don't do anything with it")
                end
            else
                req.reply(coap.internal_error(res))
                log:error("Error processing request:", res)
            end
            return
        end
    end
    log:warn("No path matches", req.path_str)
    req.reply(coap.not_found())
end)

-- Return the socket in case somebody wants to close it later.
return sock