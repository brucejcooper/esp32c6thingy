Helpers = {}


function Helpers.read_file(fname)
    local f = io.open(fname, "r")
    if not f then
        error("Could not open file" .. fname)
    end
    local contents = f:read("*a")
    f:close()
    return contents
end

function Helpers.table_to_string(t)
    assert(type(t) == 'table', "expected table")
    local sep = "{"
    local ret = ""

    for k, v in pairs(t) do
        local type = type(v)
        local sval

        if type == "table" then
            sval = Helpers.table_to_string(v)
        else
            sval = string.format("%s", v)
        end
        ret = ret .. sep .. k .. "=" .. sval
        sep = ","
    end
    return ret .. "}"
end

function Helpers.hexlify(s)
    local len = string.len(s)
    local parts = {}
    for i = 1, len do
        table.insert(parts, string.format("%02X", string.byte(s, i)))
    end
    return table.concat(parts)
end

--- Makes a deep copy of a table, so that we can mess with it without messing with the original.
function Helpers.assign(t, into)
    assert(type(t) == 'table', "expected table")
    local c = into or {}
    for k, v in pairs(t) do
        c[k] = v
    end
    return c
end

Lookup = {}
Lookup.__index = function(table, key)
    local kt = type(key)
    local val
    if kt == 'string' then
        val = table.by_str[key]
    elseif kt == 'number' then
        val = table.by_int[key]
    else
        error("index lookup must be a string or integer in the table")
    end
    if not val then
        error(string.format("Key %s not found in lookup table", key))
    end
    return val
end

function Lookup:new(items)
    local f = {
        by_int = {},
        by_str = {}
    }
    setmetatable(f, self)
    for i, entry in ipairs(items) do
        f.by_int[entry.i] = entry.s
        f.by_str[entry.s] = entry.i
    end
    return f
end

return Helpers
