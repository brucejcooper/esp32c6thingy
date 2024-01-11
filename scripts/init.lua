-- This is a default init file that is only used when building out starting firmware.
-- It will not start anything, and expects some third party process to alter it, then restart.  
require("async")


start_async_task(function()
    local res = await(Future:defer(1000, "hello"))
end)

