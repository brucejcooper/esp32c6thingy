local log = Logger:get("coap")

require("async")
require("helpers")

coap = {
    resources = {},
    next_msg_id = math.random(65536) - 1,
    ack_timeout = 2000,
    ack_random_factor = 1.5,
    max_retransmit = 4,
    active_rtrans = {},
    port = 5683
}

local coap_codes = Lookup:new {
    { i = 0x00,  s = 'empty' },
    { i = 0x01,  s = "get" },
    { i = 0x02,  s = "post" },
    { i = 0x03,  s = "put" },
    { i = 0x04,  s = "delete" },

    { i = 0x40,  s = "response_min" },
    { i = 0x41,  s = "created" },
    { i = 0x42,  s = "deleted" },
    { i = 0x43,  s = "valid" },
    { i = 0x44,  s = "changed" },
    { i = 0x45,  s = "content" },
    { i = 0x5F,  s = "continue" },

    { i = 0x80,  s = "bad_request" },
    { i = 0x81,  s = "unauthorized" },
    { i = 0x82,  s = "bad_option" },
    { i = 0x83,  s = "forbidden" },
    { i = 0x84,  s = "not_found" },
    { i = 0x85,  s = "method_not_allowed" },
    { i = 0x86,  s = "not_accpetable" },
    { i = 0x88,  s = "request_incomplete" },
    { i = 0x81C, s = "precondition_failed" },
    { i = 0x81D, s = "request_too_large" },
    { i = 0x81F, s = "unsupported_format" },

    { i = 0xA0,  s = "internal_error" },
    { i = 0xA1,  s = "not_implemented" },
    { i = 0xA2,  s = "bad_gateway" },
    { i = 0xA3,  s = "service_unavailable" },
    { i = 0xA4,  s = "gateway_timeout" },
    { i = 0xA5,  s = "proxy_not_supported" },
}

local coap_types = Lookup:new {
    { i = 0, s = "confirmable" },
    { i = 1, s = "non-confirmable" },
    { i = 2, s = "ack" },
    { i = 3, s = "reset" }
}

local coap_format = Lookup:new {
    { i = 0,   s = "text" },
    { i = 0,   s = "text" },

    { i = 16,  s = "cose-encrypt0" },
    { i = 17,  s = "cose-mac0" },
    { i = 18,  s = "cose-sign1" },
    { i = 40,  s = "link-format" },
    { i = 41,  s = "xml" },
    { i = 42,  s = "octet-strem" },
    { i = 47,  s = "exi" },
    { i = 50,  s = "json" },
    { i = 51,  s = "json-patch+json" },
    { i = 52,  s = "json-merge+json" },
    { i = 60,  s = "cbor" },
    { i = 61,  s = "cwt" },
    { i = 96,  s = "cose-encrypt" },
    { i = 97,  s = "cose-mac" },
    { i = 98,  s = "cose-sign" },
    { i = 101, s = "cose-key" },
    { i = 102, s = "cose-key-set" },
    { i = 110, s = "senml+json" },
    { i = 111, s = "sensml+json" },
    { i = 112, s = "senml+cbor" },
    { i = 113, s = "sensml+cbor" },
    { i = 114, s = "senml-exi" },
    { i = 115, s = "sensml-exi" },
    { i = 256, s = "coap-group+json" },
    { i = 310, s = "senml+xml" },
    { i = 311, s = "sensml+xml" }
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
    for i, v in ipairs({ string.byte(bstr, 1, string.len(bstr)) }) do
        val = val << 8 | v
    end
    return val
end

local function int_to_bstr(ival)
    local bytes = string.char(ival & 0xFF)
    while ival > 0xFF do
        ival = ival >> 8
        bytes = string.char(ival) .. bytes
    end
    return bytes
end



local function encode_opt_header(diff, len)
    local bytes = { 0 }
    local first

    if diff < 13 then
        first = diff << 4
    elseif diff < 269 then
        first = 13 << 4
        table.insert(bytes, diff - 13)
    else
        first = 14 << 4
        local remainder = diff - 269
        table.insert(bytes, remainder >> 8)
        table.insert(bytes, remainder & 0xFF)
    end
    if len < 13 then
        first = first | len
    elseif diff < 269 then
        first = first | 13
        table.insert(bytes, len - 13)
    else
        first = first | 14
        local remainder = len - 269
        table.insert(bytes, remainder >> 8)
        table.insert(bytes, remainder & 0xFF)
    end
    bytes[1] = first
    return string.char(table.unpack(bytes))
end


local function encode_opt_many(diff, val, opt)
    local parts = {}
    local dx = diff

    for i, v in ipairs(val) do
        local len = string.len(v)
        table.insert(parts, encode_opt_header(dx, len))
        table.insert(parts, v)
        dx = 0 -- Inherently they're all the same option
    end
    return table.concat(parts)
end

local function encode_opt_singleton(diff, val)
    return encode_opt_header(diff, string.len(val)) .. val
end

local function parse_opt_block(packet, opt_name, val)
    if packet[opt_name] then
        error(string.format("singleton option %s appears more than once", opt_name))
    end
    local ival = bstr_to_int(val)
    packet[opt_name] = {
        id = ival >> 4,
        size = 1 << (4 + (ival & 0x07)),
        more = (ival & 0x08) ~= 0
    }
end

local function encode_opt_block(diff, val)
    local ival = val.size

    if ival == 1024 then
        ival = 6
    elseif val == 512 then
        ival = 5
    elseif val == 256 then
        ival = 4
    elseif val == 128 then
        ival = 3
    elseif val == 64 then
        ival = 2
    elseif val == 32 then
        ival = 1
    elseif val == 16 then
        ival = 0
    else
        error("Invalid size")
    end
    if val.more then
        ival = ival | 0x08
    end
    ival = ival | val.id << 4

    local bytes = int_to_bstr(ival)
    return encode_opt_header(diff, string.len(bytes)) .. bytes
end


local function parse_opt_lookup(packet, opt_name, val, opt)
    packet[opt_name] = opt.lookup[bstr_to_int(val)]
end

local function encode_opt_lookup(diff, val, opt)
    local ival = opt.lookup[val]
    local bytes = int_to_bstr(ival)
    return encode_opt_header(diff, string.len(bytes)) .. bytes
end


-- List of all options, in the order that they must appear (numeric) - we can't use a LUT cos that doesn't preserve order
local ordered_options = {
    { id = 1,  sval = "if_match",       parse = parse_opt_many,      encode = encode_opt_many },
    { id = 3,  sval = "uri_host",       parse = parse_opt_many,      encode = encode_opt_many },
    { id = 4,  sval = "e_tag",          parse = parse_opt_singleton, encode = encode_opt_many },
    { id = 5,  sval = "if_none_match",  parse = parse_opt_singleton, encode = encode_opt_singleton },
    { id = 6,  sval = "observe",        parse = parse_opt_many,      encode = encode_opt_many },
    { id = 7,  sval = "uri_port",       parse = parse_opt_many,      encode = encode_opt_many },
    { id = 8,  sval = "location_path",  parse = parse_opt_many,      encode = encode_opt_many },
    { id = 11, sval = "path",           parse = parse_opt_many,      encode = encode_opt_many },
    { id = 12, sval = "format",         parse = parse_opt_lookup,    encode = encode_opt_lookup,   lookup = coap_format },
    { id = 14, sval = "max_age",        parse = parse_opt_many,      encode = encode_opt_many },
    { id = 15, sval = "query",          parse = parse_opt_many,      encode = encode_opt_many },
    { id = 17, sval = "accept",         parse = parse_opt_many,      encode = encode_opt_many },
    { id = 20, sval = "location_query", parse = parse_opt_many,      encode = encode_opt_many },
    { id = 23, sval = "block2",         parse = parse_opt_block,     encode = encode_opt_block },
    { id = 27, sval = "block1",         parse = parse_opt_block,     encode = encode_opt_block },
    { id = 28, sval = "size2",          parse = parse_opt_many,      encode = encode_opt_many },
    { id = 35, sval = "proxy_uri",      parse = parse_opt_many,      encode = encode_opt_many },
    { id = 39, sval = "proxy_scheme",   parse = parse_opt_many,      encode = encode_opt_many },
    { id = 60, sval = "size1",          parse = parse_opt_many,      encode = encode_opt_many },
}
-- Turn it into an easy lookup for decoding.
local opt_by_name = {}
for i, opt in ipairs(ordered_options) do
    opt_by_name[opt.id] = opt;
end



local CoAPReader = {}
CoAPReader.__index = CoAPReader
function CoAPReader:new(b)
    local r = {
        buf = b,
        pos = 1,
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
    local b = string.sub(self.buf, self.pos, self.pos + n - 1)
    self.pos = self.pos + n
    return b
end

function CoAPReader:u16()
    local b1, b2 = string.byte(self.buf, self.pos, self.pos + 1)
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
    local opt_meta = opt_by_name[self.last_opt]
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

local function parse_coap_packet(buf)
    local r = CoAPReader:new(buf)

    local firstbyte = r:u8()
    local version = firstbyte >> 6
    assert(version == 1, "Invalid CoAP version")
    local code_num = r:u8()
    local code = coap_codes[code_num]
    assert(code, "Invalid code")

    local token_len = firstbyte & 0x0F;
    log:debug("Parsing CoAP request first byte is", firstbyte, "token length", token_len, "code is", code);
    local packet = {
        type = coap_types[((firstbyte >> 4) & 0x03)],
        code = code,
        message_id = r:u16(),
        token = r:bytes(token_len),
        path = {},
    }
    local opt, opt_data = r:opt()
    while opt do
        log:debug("Processing option ", opt.sval, opt_data)
        opt.parse(packet, opt.sval, opt_data, opt)
        opt, opt_data = r:opt()
    end

    if r.payload_marker_parsed then
        packet.payload = r:remainder()
    end
    return packet
end


local function encode_coap_packet(pkt)
    log:debug("Encoding", pkt)
    assert(pkt.message_id ~= nil)
    assert(pkt.token ~= nil)
    local token = pkt.token
    assert(string.len(token) < 16)
    local typecode = coap_types[pkt.type]
    assert(typecode ~= nil)

    local codecode = coap_codes[pkt.code]
    assert(codecode ~= nil, "bad code", pkt.code)

    -- Start with the header
    local parts = {
        string.char(
            0x40 | (typecode << 4) | string.len(token),
            codecode,
            pkt.message_id >> 8,
            pkt.message_id & 0xFF
        ),
        pkt.token
    }

    -- Add any options
    local last_opt_val = 0
    for i, opt in ipairs(ordered_options) do
        local opt_val = pkt[opt.sval]
        if opt_val then
            table.insert(parts, opt.encode(opt.id - last_opt_val, opt_val, opt))
            last_opt_val = opt.id
        end
    end
    if pkt.payload then
        assert(type(pkt.payload == 'string'))
        table.insert(parts, "\xFF")
        table.insert(parts, pkt.payload)
    end

    return table.concat(parts)
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


local function new_token()
    local lval = math.random(0x100000000) - 1
    return string.char((lval >> 24) & 0xFF, (lval >> 16) & 0xFF, (lval >> 8) & 0xFF, lval & 0xFF)
end


local function payload_len(pkt)
    if type(pkt) == 'table' and pkt.payload then
        return string.len(pkt.payload)
    else
        return -1
    end
end


---Called each time a reliable message fails to receive an ack within its timeout.
local function rtrans_timeout(timer)
    local rtrans = timer.rtrans

    if rtrans.retransmit_count >= coap.max_retransmit then
        log:error("Giving up on transmitting packet", rtrans.packet)
        rtrans:cancel()
    else
        log:info("retrying transmission of CoAP confirmable", rtrans.packet)
        rtrans.retransmit_count = rtrans.retransmit_count + 1
        rtrans.retransmit_delay = rtrans.retransmit_delay * 2
        openthread.send_dgram(rtrans.sock_addr, rtrans.sock_port, rtrans.peer_addr, rtrans.peer_port, rtrans.encoded)
        timer:start(rtrans.retransmit_delay)
    end
end

---For a given confirmable transmission, we correlate acks based on a compbination of the peer address, port and message id.
---@param pkt any
---@return unknown
local function correlation_id(pkt)
    return pkt.peer_addr .. pkt.peer_port .. pkt.message_id
end

---Stops any more attempts at retransmitting the reliable packet.
local function rtrans_stop_retranmsission(rtrans)
    rtrans.timer.stop()
    rtrans.timer.delete()
    coap.active_rtrans[rtrans.correlation_id] = nil
end

local function add_default_fields(pkt)
    if not pkt.peer_addr then
        error("Packet must have a peer_addr")
    end
    if not pkt.message_id then
        pkt.message_id = coap.next_msg_id
        coap.next_msg_id = (coap.next_msg_id + 1) % 65536
    end
    pkt.token = pkt.token or new_token()
    pkt.sock_port = pkt.sock_port or coap.port
    pkt.peer_port = pkt.peer_port or coap.port
    pkt.sock_addr = pkt.sock_addr or openthread.ip_addresses()[1]
end


--- Sends a non-confirmable CoAP Packet. No retransmits, no timers... nothing.
function coap:send_non_confirmable(pkt)
    add_default_fields(pkt)
    if not pkt.type then
        pkt.type = "non-confirmable"
    end
    local encoded = encode_coap_packet(pkt)
    openthread.send_dgram(pkt.sock_addr, pkt.sock_port, pkt.peer_addr, pkt.peer_port, encoded)
end

--- Sends a CoAP packet using the confirmable packet type.  Uses timers to ensure that the packet is
---@param pkt CoapPacket a table storing information about the packet that is to be sent.
---@return Future A future that will be resolved with response packet.
function coap:send_confirmable(pkt)
    add_default_fields(pkt)
    local timer = Timer:new(rtrans_timeout)
    local rtrans = Future:new {
        packet = pkt,
        sock_addr = pkt.sock_addr,
        sock_port = pkt.sock_port,
        peer_addr = pkt.peer_addr,
        peer_port = pkt.peer_port,
        encoded = encode_coap_packet(pkt),
        timer = timer,
        correlation_id = correlation_id(pkt),
        retransmit_count = 0,
        retransmit_delay = math.random(coap.ack_timeout, coap.ack_timeout * coap.ack_random_factor),
        cancel = rtrans_stop_retranmsission
    }
    coap.active_rtrans[rtrans.correlation_id] = rtrans
    timer.rtrans = rtrans
    openthread.send_dgram(rtrans.sock_addr, rtrans.sock_port, rtrans.peer_addr, rtrans.peer_port, rtrans.encoded)
    timer:start(rtrans.retransmit_delay)
    return rtrans
end

---A simple COAP handler that looks up handlers in the coap.resources field.
---@param dgram table an object with a body string that represents the received datagram
function coap:handle_coap_packet(dgram)
    log:debug("Received packet on coap port");
    local pkt = parse_coap_packet(dgram.body)
    pkt.peer_addr = dgram.peer_addr
    pkt.peer_port = dgram.peer_port
    pkt.sock_addr = dgram.sock_addr
    pkt.sock_port = dgram.sock_port
    log:debug("parsed", pkt);

    local msg_type = pkt.type
    if msg_type == "confirmable" or msg_type == "non-confirmable" then
        -- log:debug("DGRAM", Helpers.hexlify(dgram.body), "packet", req.block2)
        log:debug("Parsed", pkt)

        pkt.reply = function(resp)
            -- Wrap the respone, if it isn't already in the right format.
            if type(resp) ~= 'table' then
                resp = { payload = resp }
            end
            if msg_type == "confirmable" then
                resp.type = "ack"
            else
                resp.type = "non-confirmable"
            end
            -- Copy message_id and token from the request
            resp.token = pkt.token
            resp.message_id = pkt.message_id
            -- Infer a response code, if one has not yet been set
            if not resp.code then
                if pkt.code == 'get' then
                    resp.code = 'content'
                elseif pkt.code == 'put' or pkt.code == 'post' then
                    resp.code = 'changed'
                elseif pkt.code == 'delete' then
                    resp.code = 'deleted'
                else
                    error("can not infer response code")
                end
            end
            -- Send a datagram with the encoded respopnse
            pkt.replied = true
            local bytes = encode_coap_packet(resp)
            openthread.send_dgram(pkt.sock_addr, pkt.sock_port, pkt.peer_addr, pkt.peer_port, bytes)
            -- Log the request
            log:info(string.format("%s %s %s %d - %s %d", openthread.ip6_to_str(pkt.peer_addr), pkt.code,
                pkt.path_str, payload_len(pkt), resp.code, payload_len(resp)));
        end

        pkt.path_str = table.concat(pkt.path, "/")
        local methods = lookup(pkt.path)
        if methods then
            local operation = methods[pkt.code]
            if operation then
                log:debug("invoking operation", operation.desc)
                local success, res = xpcall(operation.handler, function(err)
                    -- Send the traceback along with the error
                    return debug.traceback(err, 2)
                end, pkt)
                if success then
                    if not pkt.replied then
                        log:warn("Handler did not reply.  Message will not be responded to")
                    end
                    if res ~= nil then
                        log:warn("Handler returned a value, but we don't do anything with it")
                    end
                else
                    pkt.reply { code = "internal_error", payload = res }
                    log:error("Error processing request:", res)
                end
                return
            end
        end
        log:warn("No path matches", pkt.path_str)
        pkt.reply { code = "not_found" }
    elseif msg_type == "ack" then
        local rtrans = coap.active_rtrans[correlation_id(pkt)]
        if rtrans then
            log:info(string.format("Ack from %s port %d msgId %d", openthread.ip6_to_str(pkt.peer_addr),
                pkt.peer_port, pkt.message_id))
            rtrans:cancel()
            rtrans.set(pkt)
        else
            -- We don't know about this packet.
            log:info(string.format("Ack from unknown %s port %d msgId %d will be ignored",
                openthread.ip6_to_str(pkt.peer_addr), pkt.peer_port, pkt.message_id))
        end
    elseif msg_type == "reset" then
        local rtrans = coap.active_rtrans[correlation_id(pkt)]
        if rtrans then
            log:warn("Received reset message for active call to", pkt.peer_addr, "for message_id", pkt.message_id)
            rtrans:cancel()
        else
            log.warn("Received reset for ongoing transmission that we don't recognise")
        end
    else
        log:warn("Received packet of unknown type", msg_type)
    end
end

function coap:start_server()
    log:info("Starting CoAP server")
    self.sock = openthread:listen_udp(coap.port, coap.handle_coap_packet)
end

-- Return the socket in case somebody wants to close it later.
return coap
