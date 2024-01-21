require("async")
local log = Logger:get("button")
--- @class Button
-- Has a state machine that interacts with the gpio system and timers to do debouncing, double click and long press detection.
-- Implementors can react to events by overriding the on_* methods.
Button = {}

---@class GpioFuture A GPIO specific future that will resolve when the specified pin goes to the supplied level.
local GpioFuture = {}

---comment
---@param pin integer a value between 0 and 32 that represents the ESP32 pin number
---@param level "low"|"high" What level you're looking for
---@return GpioFuture The instance
function GpioFuture:new(pin, level)
    local f = Future:new {
        pin = pin,
        on_set = GpioFuture.on_set,
        cancel = GpioFuture.cancel
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

---Creates a new button instance.  Each button starts a co-routine that will loop forever
---(taking advantage of tail calls), handling clicks, long presses and
---@return Button the button instance.
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

---Called when pressed or released to ignore any noise caused by bouncy switches.
---@return integer the level of the switch after this delay (to ensure that its still at the right level)
function Button:debounce()
    -- Do a debounce (ignore input for a bit)
    await(self.timer:defer(75))
    -- If it is still pressed, then we have a valid button press
    return gpio.get(self.pin)
end

-- Each Method below represents a button state. It takes advantage of tail calls to not fill up the stack, but
-- will hop from method to method representing each action that occurs.

---Called when the button is released, and we're waiting for it to be pressed.  Does debuonching, then moves onto pressed state
function Button:wait_for_press()
    while true do
        -- Wait for pin to go to 0 (pressed)
        await(GpioFuture:new(self.pin, "low"))
        -- Button must still be pressed after our debounce period. If it isn't we simply start waiting again.
        if (self:debounce() == 0) then
            return self:pressed();
        else
            self:emit_evt("on_click")
        end
    end
end

--- Called after the button is pressed and debounced.
function Button:pressed()
    self:emit_evt("on_press")
    self.repeat_count = 0;

    -- wait for the button to be released, or for the initial delay to have occurred.
    if await(GpioFuture:new(self.pin, "high"), self.timer:defer(self.init_delay)) then
        return self:released()
    else
        return self:long_pressed()
    end
end

function Button:emit_evt(evt)
    local handler = self[evt]
    if handler then
        handler(self)
    end
end

--- Called after the button is released (but not yet debounced)
function Button:released()
    -- Button was released
    if (self.repeat_count == 0) then
        self:emit_evt("on_click")
    else
        self:emit_evt("on_release")
    end
    self:debounce()
    return self:wait_for_press()
end

--- Called when the button is held down for a long period of time.  Will be called repeatedly (each repeat_delay ms) until the button is released.
function Button:long_pressed()
    local released = false
    repeat
        log:info("Long Press")
        self.repeat_count = self.repeat_count + 1
        self:emit_evt("on_long_press")
        -- Wait for the repeat timeout to occur, or for the button to have been released
        released = await(GpioFuture:new(self.pin, "high"), self.timer:defer(self.repeat_delay))
    until released
    return self:released()
end

return Button
