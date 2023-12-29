
devices = {}



function register_device(d) 
    if (devices[d.id]) then
        error("Device with that ID already registered")
    end
    devices[d.id] = d 
    -- TODO notify the system that a new device exists.
end


function deregister_device(devid) 
    devices[devid] = nil
    -- TODO notify the system that a device has been removed.  Perhaps this should be a C function....
end



return devices