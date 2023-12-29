require("await")
require("event_loop")
button = {}

--- 
-- Creates a new button instance.  Each button starts a co-routine that will loop forever
-- (taking advantage of tail calls), handling clicks, long presses and 
function button:new(pin) 
    print("creating new button on pin", pin)
    local b = {
        pin=pin,
        init_delay=1000,
        repeat_delay=250,
        repeat_count = 0,
    }
    setmetatable(b, self)
    self.__index = self

    -- Configure pin as an input.
    gpio.config_input(b.pin);

    -- Start a coroutine that will run forever processing the button state.
    event_loop.start_coro(coroutine.create(
        function()
            return b:wait_for_press()
        end
    ))
    return b
end


---
-- Called when pressed or released to ignore any noise caused by bouncy switches.  
-- Returns the level of the switch after this delay (to ensure that its still at the right level)
function button:debounce()
    -- Do a debounce (ignore input for a bit)
    await{ timeout=75 }
    -- If it is still pressed, then we have a valid button press
    return gpio.get(self.pin)
end

-- Each Method below represents a button state. It takes advantage of tail calls to not fill up the stack, but
-- will hop from method to method representing each action that occurs. 

---
-- Called when the button is released, and we're waiting for it to be pressed.  Does debuonching, then moves onto pressed state
function button:wait_for_press() 
    while true do
        -- Wait for pin to go to 0 (pressed)
        print("Waiting for press on button pin", self.pin)
        await{ pin=self.pin, level=0 }
        -- Button must still be pressed after our debounce period. If it isn't we simply start waiting again.
        if (self:debounce() == 0) then
            return self:pressed();
        else
            self:clicked()
        end
    end
end

---
-- Called after the button is pressed and debounced.
function button:pressed()
    print("Button pressed at pin", self.pin)
    self.repeat_count = 0;

    -- wait for the button to be released, or for the initial delay to have occurred.
    if await{ pin=self.pin, level=1, timeout=self.init_delay } ~= -1 then
        return self:released()
    else
        return self:long_pressed()
    end
end

function button:clicked()
    print("Button clicked at pin", self.pin)
    -- TODO Take action!
end

---
-- Called after the button is released (but not yet debounced)
function button:released()
    -- Button was released
    if (self.repeat_count == 0) then
        self:clicked()
    else
        print("Button released at pin", self.pin)
    end
    self:debounce()
    return self:wait_for_press()
end

---
-- Called when the button is held down for a long period of time.  Will be called repeatedly (each repeat_delay ms) until the button is released.
function button:long_pressed()
    local released = false
    repeat
        self.repeat_count = self.repeat_count + 1
        print("Button long-held at pin", self.pin, self.repeat_count, "times")
        -- Wait for the repeat timeout to occur, or for the button to have been released
        released = await{ pin=self.pin, level=1, timeout=self.repeat_delay } ~= -1
    until released
    return self:released()
end

return button