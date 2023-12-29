-- require("dali_bus");
require("button")
require("await")


-- The configuration of this device is to have 5 buttons
button:new(19)
button:new(20)
button:new(21)
button:new(22)
button:new(23)

-- controller = dali_bus:new(12, 13);

-- system.run_coro(coroutine.create(
--     function()
--         print("Doing initial scan of Dali devices")
--         dali_bus.scan()
--     end
-- ))
