local log = Logger:new("event_loop")

---Represents a singleton object that will manage running co-routines with the concept of futures. 
---A future is a thing that will be set in the future (usually by a callback from C).  a task function
---(passed to EventLoop.start) may do a coroutine.yield on a Future to wait for it to be set, then continue
---execution once that future resolves. As a syntatic convinence, EventLoop.await will do that yield for you. 
---It is possible to wait on one of multiple futures by using Future:oneof - This will call the cancel method
---on all but the first future to resolve, and will resovle to that first one. 
EventLoop = {
    waiting_tasks = {}
}
local Task = {}
Task.__index = Task


---@class Future
Future = {}
Future.__index = Future

function Future:new(init)
    local instance = init or {}
    instance.state = "pending"
    setmetatable(instance, self)
    return instance
end

local function oneof_child_resolved(child_future, val)
    -- Cancel all the other choices, if possible.
    for i,childf in ipairs(child_future.parent.choices) do
        if childf ~= child_future and childf.cancel then
            childf.cancel(childf)
        end
    end
    -- Set our value to the same value as the child
    child_future.set(val)

end

---Creates a future that will resolve to the first one that resolves from a set of choices
---If you pass in just one future, that will be returned as an optimisation
---@param futurelist table<Future> list of futures that will comprise this future
---@return Future
function Future:oneof(...)
    local num = select('#', ...)
    assert(num > 0)
    local varargs = {...}
    if num == 1 then
        return varargs[1]
    end
    local f = Future:new({ choices=varargs})
    for i,choice in ipairs(arg) do
        choice.parent = f
        choice.on_set = oneof_child_resolved
    end
    return f
end

function Future:set(val)
    assert(self.state == "pending", "Condition already completed")
    self.state = "set"
    self.value = val
    if self.on_set then
        self.on_set(self, val)
    end
end


local function cancel_deferred_future(f)
    f.timer:stop()
    f.timer:delete()
end

local function deferred_future_timer_fired(t)
    t:delete()
    t.future:set(t.future.value)
end


---Creates a future that will be set after a specified delay (unless cancelled first)
---@param timeout integer the number of milliseconds that the future will be deferred for
---@param value any the value that the future will be set to when the timer goes off
function Future:defer(timeout, value)
    local timer = Timer:new(deferred_future_timer_fired)
    local f = Future:new{
        timer=timer,
        value=value,
        cancel=cancel_deferred_future
    }
    timer.future = f
    timer:start(timeout)
    return f
end


function EventLoop:start(fn, firstarg)
    local task = {
        loop=EventLoop,
        thread=coroutine.create(fn)
    }
    setmetatable(task, Task)
    local success, future = coroutine.resume(task.thread, firstarg)

    if success then
        if not getmetatable(future) == Future then
            error(string.format("Initial call of task %s returned an object that isn't a future: %s", task, future))
        end
        task:set_future(future)
        return task
    else
        log:error("Error performing first execute of task", future)
    end
end

---Called from within an EventLopp task - this will suspend activity until one of the supplied futures resolves. 
---@param ... Future A set of futures that the loop will wait for
function EventLoop:await(...)
    local ret = coroutine.yield(Future:oneof(...))
    return ret;
end

---Glue function to notify the task that the future was set withoug creating a closure
---@param future Future The future that had its value set
---@param res any The value it was set to. 
local function task_future_set(future, res)
    future.task:continue_execution(res)
end

function Task:set_future(future)
    log:debug("setting task", self, "'s future to ", future)
    future.task = self
    self.awaiting = future
    future.on_set = task_future_set
    EventLoop.waiting_tasks[self] = future
end


---called when a suspended task's condition is triggered
---@param future_result any
function Task:continue_execution(future_result)
    local success, callresult = coroutine.resume(self.thread, future_result)
    local status = coroutine.status(self.thread)
    if success then
        if status == "dead" then
            -- Coroutine successfully completed.
            if callresult ~= nil then
                log:warn("Task returned value", callresult, ". It will be ignored")
            else    
                log:debug("Task", self, "successfully completed");
            end
            EventLoop.waiting_tasks[self] = nil
        else
            -- The coroutine is still running, and returned another future.
            self:future_completed(callresult)
        end
    else
        -- There was a problem running the task
        if self.error_handler then
            self.error_handler(self, callresult)
        else
            log:error("Unhandled error running task:", callresult)
        end
    end
end

return EventLoop