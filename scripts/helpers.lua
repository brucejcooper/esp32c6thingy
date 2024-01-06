
Helpers = {}

function Helpers.table_to_string(t)
    local sep = "{"
    local ret = ""

    for k,v in pairs(t) do
        local type = type(v)
        local sval

        if type == "table" then
            sval = Helpers.table_to_string(v)
        else
            sval = string.format("%s", v)
        end
        ret = ret..sep..k.."="..sval
        sep = ","
    end
    return ret.."}"
end


return Helpers