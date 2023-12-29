function await(condition)
    return coroutine.yield(condition)
end

return await