require("file_server")
require("async")
require("helpers")

local log = Logger:get("default_handlers")


function cbor.encode_values_as(src, hints)
    -- Make a shallow copy of the table
    local copy = Helpers.assign(src, {})
    -- Set a metatable with cbor encoding hints
    setmetatable(copy, { __valenc = hints })
    return copy
end

coap.resources[{ "restart" }] = {
    post = {
        desc = "restarts device",
        handler = function(req)
            -- We run the restart in a separate task after a small delay so that the coroutine can return
            start_async_task(function()
                log:warn("Restarting Device");
                await(Future:defer(250))
                system.restart()
            end)
            req.reply { code = "changed" }
        end
    }
}


coap.resources[{ "info" }] = {
    get = {
        desc = "fetches device info",
        handler = function(req)
            local info = {
                esp_idf_ver = system.esp_idf_ver,
                uptime = system.uptime() / 1000,
                mac_address = system.mac_address,
                reset_reason = system.reset_reason,
                heap = system.heap_info(),
                firmware = cbor.encode_values_as(system.firmware, { sha256 = 'bstr' })
            }
            req.reply { code = "content", format = "cbor", payload = cbor.encode(cbor.encode_values_as(info, { mac_address = 'bstr' })) };
        end
    }
}


-- This matches the root path (no path options supplied)
coap.resources[{}] = {
    get = {
        desc = "lists all resource handlers",
        handler = function(req)
            local resourceList = cbor.encode_as_list {}
            for path, methods in pairs(coap.resources) do
                local r = {
                    path = cbor.encode_as_list(Helpers.assign(path)),
                }
                for method, op in pairs(methods) do
                    r[method] = op.desc or "No description"
                end
                table.insert(resourceList, r)
            end
            req.reply { code = "content", format = "cboar", payload = cbor.encode(resourceList) };
        end
    }
}

coap.resources[{ "log", "*" }] = {
    get = {
        desc = "gets the log threshold",
        handler = function(req)
            local tag = req.path[2]
            log:info("Getting log level for ", tag)
            req.reply(Logger:get(tag).level)
        end
    },
    put = {
        desc = "sets the log threshold",
        handler = function(req)
            local tag = req.path[2]
            log:info("Setting log level for ", tag, "to", req.payload)
            Logger:get(tag).level = req.payload
            req.reply { code = "changed" }
        end
    }
}


coap.resources[{ "test" }] = {
    get = {
        desc = "test encoding",
        handler = function(req)
            req.reply {
                code = "content",
                format = "cbor",
                payload = cbor.encode("blah")
            }
        end
    }
}
