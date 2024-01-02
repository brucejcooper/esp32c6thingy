
---Returns information about the device
---@param req table The COAP message supplied by openthread
---@return any The body that will be returned to the caller
coap.resource("info", function(msg)
    if msg.code == coap.CODE_GET then
        log.info("GET of info resource")
        return {
            firmware_version="1.0",
            uptime=0
        }
    else
        msg.response_code = coap.CODE_METHOD_NOT_ALLOWED
    end
end)

---Restarts the device after a short delay.  The delay is to give the COAP server the opportunity to respond to the request before the reset occurs.
---@param msg table The COAP message supplied by openthread
coap.resource("restart", function(msg)
    if msg.code == coap.CODE_POST then
        system.start_task(function()
            log.info("Restarting");
            system.await({ timeout = 250 }) -- Give openthread a chance to send the response before we reboot
            system.restart()
        end)
        msg.response_code = coap.CODE_CHANGED -- This is the default, but its nice to be explicit.
    else
        msg.response_code = coap.CODE_METHOD_NOT_ALLOWED
    end
end)
