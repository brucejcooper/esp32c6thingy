local log = Logger:get("async")

---@class Task
---Represents a mechanism for async await using coroutines.  Start a task by passing a function to start_async_task.
---This function can then yield on any Future created by a function etc... If you want to wait on
---multiple conditions, then use Future.oneof(), or just pass multiple arguments to await
-- As a 
local Task = {}
Task.__index = Task




---@class Future an object that will have its value set at some point in the future.  Tasks may await futures.
---@field on_set function callback which is called when the value is set
Future = {}
Future.__index = Future



---------------------------------- Future Methods ----------------------------------

function Future:new(init)
    local instance = init or {}
    instance.is_set = false
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
    child_future.parent:set(val)

end

---Creates a future that will resolve to the first one that resolves from a set of choices
---@param ... Future list of at least one future that will comprise this future - If you just pass one it will return that.
---@return Future
function Future:oneof(...)
    local num = select('#', ...)
    assert(num > 0)
    local varargs = {...}
    if num == 1 then
        return varargs[1]
    end
    local f = Future:new({ choices=varargs})
    for i,choice in ipairs(varargs) do
        choice.parent = f
        choice.on_set = oneof_child_resolved
    end

    -- If one of the choices ia already set, so is the oneof future.
    -- Done in a separate loop so that we will cancel other futures.
    for i,choice in ipairs(varargs) do
        if choice.is_set then
            f:set(choice.value)
        end
    end
    return f
end

---Sets the value of the future, and notifies via the on_set field of the Future, if set.
---@param val any The value we are setting.
function Future:set(val)
    assert(not self.is_set, "Condition already completed")
    self.is_set = true
    self.value = val
    if self.on_set then
        self.on_set(self, val)
    end
end


local function cancel_deferred_future(f)
    f.timer:stop()
    if not f.reusable then
        f.timer:delete()
    end
end

local function deferred_future_timer_fired(t)
    local f = t.future
    if not f.reusable then
        t:delete()
    end
    f:set(f.deferred_value)
end


---Creates a future that will be set after a specified delay (unless cancelled first)
---@param timeout integer the number of milliseconds that the future will be deferred for
---@param value any? the value that the future will be set to when the timer goes off
---@param reuseable_timer Timer? By default, the future will create and delete a new timer each time.  To make things more efficiennt, you can pass in a reusable timer that will be re-used
function Future:defer(timeout, value, reuseable_timer)
    local timer
    local reusable = false
    if reuseable_timer then
        timer = reuseable_timer
        timer.on_timeout = deferred_future_timer_fired
        reusable = true
    else
        timer = Timer:new(deferred_future_timer_fired)
    end
    local f = Future:new{
        timer=timer,
        deferred_value=value,
        cancel=cancel_deferred_future,
        reusable=reusable
    }
    timer.future = f
    timer:start(timeout)
    return f
end

---------------------------------- Task Methods ----------------------------------

---Starts a coroutine from the supplied function that can yeild Futures.  Each time the function yeilds a 
---future, it will be resumed with the result of the future when that future resolves. 
---@param fn function a function that execute the task 
---@param arg any a single argument that will be passed to the function to start things.  A good way to pass context
function start_async_task(fn, arg)
    local task = {
        thread=coroutine.create(fn)
    }
    setmetatable(task, Task)
    local success, future = coroutine.resume(task.thread, arg)

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
---@param ... Future A set of futures that the loop will wait for. The first one to resolve will cause the cancellation of all othes, and will have its value returned to the task
function await(...)
    local f = Future:oneof(...)
    -- If the future is already set, then short-circuit.
    if f.is_set then
        return f.value
    end
    local ret = coroutine.yield(f)
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
        else
            -- The coroutine is still running, and returned another future.
            if (getmetatable(callresult) == Future) then
                self:set_future(callresult)
            else
                log:error("Async task returned something that isn't a future");
            end
        end
    else
        -- There was a problem running the task
        log:error("Unhandled error running task:", callresult )
        log:error(debug.traceback(self.thread))
    end
end