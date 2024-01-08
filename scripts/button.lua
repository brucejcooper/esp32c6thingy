require("async")
--- @class Button
-- Has a state machine that interacts with the gpio system and timers to do debouncing, double click and long press detection.
-- Implementors can react to events by overriding the on_* methods. 
Button = {}

local log = Logger:new("button")


local GpioFuture = {}

function GpioFuture:new(pin, level)
    local f = Future:new{
        pin=pin,
        on_set=GpioFuture.on_set,
        cancel=GpioFuture.cancel
    }
    -- Don't set the metadatatable as future has its own.
    gpio.set_pin_isr(pin, level, self.isr_handler, f)
    return f
end

function GpioFuture:isr_handler()
    self:cancel()
    self:set(self.pin)
end

function GpioFuture:cancel()
    gpio.set_pin_isr(self.pin, "disable");
end


--- 
-- Creates a new button instance.  Each button starts a co-routine that will loop forever
-- (taking advantage of tail calls), handling clicks, long presses and 
function Button:new(initval) 
    if not initval.pin then
        error("No pin specified")
    end
    local b = initval
    if not b.init_delay then
        b.init_delay = 1000
    end
    if not b.repeat_delay then
        b.repeat_delay = 250
    end
    b.repeat_count = 0
    setmetatable(b, self)
    self.__index = self
    -- Create a re-usable timer that by default does nothing. It'll be re-used for each Wait that the state machine does
    self.timer = Timer:new(function() end)

    -- Configure pin as an input.
    gpio.config_input(b.pin);

    -- Start a coroutine that will run forever processing the button state.
    start_async_task(b.wait_for_press, b)
    return b
end


---
-- Called when pressed or released to ignore any noise caused by bouncy switches.  
-- Returns the level of the switch after this delay (to ensure that its still at the right level)
function Button:debounce()
    -- Do a debounce (ignore input for a bit)
    await(Future:defer(75, -1, self.timer))
    -- If it is still pressed, then we have a valid button press
    local lvl = gpio.get(self.pin)
    return lvl;
end

-- Each Method below represents a button state. It takes advantage of tail calls to not fill up the stack, but
-- will hop from method to method representing each action that occurs. 

---
-- Called when the button is released, and we're waiting for it to be pressed.  Does debuonching, then moves onto pressed state
function Button:wait_for_press() 
    while true do
        -- Wait for pin to go to 0 (pressed)
        await(GpioFuture:new(self.pin, "low"))
        -- Button must still be pressed after our debounce period. If it isn't we simply start waiting again.
        if (self:debounce() == 0) then
            return self:pressed();
        else
            self:on_click()
        end
    end
end

---
-- Event method called whenever the button is clicked (presse then released before the long click delay)
function Button:on_click()
end

---
-- Event function called when the button is first pressed.
function Button:on_press()
end

---
-- Event method called when the button is released, whether it was long clicked or not.
function Button:on_release()
end

---
-- Event method called if the button is pressed for a long period, then repeatedly after that based on the timer value passed in.
function Button:on_long_press()
end


---
-- Called after the button is pressed and debounced.
function Button:pressed()
    self:on_press()
    self.repeat_count = 0;

    -- wait for the button to be released, or for the initial delay to have occurred.
    if await(GpioFuture:new(self.pin, "high"), Future:defer(self.init_delay, -1, self.timer)) ~= -1 then
        return self:released()
    else
        return self:long_pressed()
    end
end

---
-- Called after the button is released (but not yet debounced)
function Button:released()
    -- Button was released
    if (self.repeat_count == 0) then
        self:on_click()
    else
        self:on_release()
    end
    self:debounce()
    return self:wait_for_press()
end

---
-- Called when the button is held down for a long period of time.  Will be called repeatedly (each repeat_delay ms) until the button is released.
function Button:long_pressed()
    local released = false
    repeat
        self.repeat_count = self.repeat_count + 1
        self:on_long_press()
        -- Wait for the repeat timeout to occur, or for the button to have been released
        released = await(Future:oneof(GpioFuture:new(self.pin, "high"), Future:defer(self.repeat_delay, -1, self.timer))) ~= -1
    until released
    return self:released()
end

return Button