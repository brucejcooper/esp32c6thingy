
local log=Logger:new("button_actions")

---handler to make a call to the target device via COAP.
---@param b Button The button that caused the action to occur
function toggle_device(b)
    log:info("Button toggled", b.pin)
end

---handler to make a call to the target device via COAP.
---@param b Button The button that caused the action to occur
function dim_device(b)
    log:info("Button dimmed", b.pin)
end