-- Deliberately tiny conditional experiment state machine.

return function(options)
    local core = options.core
    local json_string = core.json_string
    local write_record = options.write_record
    local write_state = options.write_state
    local current_frame = options.current_frame
    local read_u8 = core.read_u8
    local read_u16 = core.read_u16
    local read_u32 = core.read_u32
    local signed_u16 = core.signed_u16
    local read_register = core.read_register
    local press_token = options.press_token
    local expand_schedule = options.expand_schedule

    local function split(value, separator)
        local result = {}
        for item in (value .. separator):gmatch("(.-)" .. separator) do
            result[#result + 1] = item
        end
        return result
    end

    local spec = os.getenv("OPENALADDIN_EXPERIMENT_ACTIONS") or ""
    local actions = {}
    local action_index = 1
    if spec ~= "" then
        for _, encoded in ipairs(split(spec, ";")) do
            local parts = split(encoded, "|")
            if parts[1] == "schedule" then
                actions[#actions + 1] = {
                    kind = "schedule",
                    tokens = expand_schedule(parts[2] or "none"),
                    position = 1
                }
            elseif parts[1] == "input" then
                actions[#actions + 1] = {
                    kind = "input",
                    token = parts[2] or "none",
                    remaining = tonumber(parts[3] or "1") or 1
                }
            elseif parts[1] == "wait" then
                actions[#actions + 1] = {
                    kind = parts[2] or "memory",
                    address = tonumber(parts[3] or "0") or 0,
                    width = parts[4] or "u8",
                    operation = parts[5] or "eq",
                    value = tonumber(parts[6] or "0") or 0,
                    remaining = tonumber(parts[7] or "120") or 120
                }
            elseif parts[1] == "marker" then
                actions[#actions + 1] = { kind = "marker", name = parts[2] or "unnamed" }
            end
        end
    end

    local function condition_value(action)
        if action.kind == "pc" then return read_register("PC") or 0 end
        local width = action.width:lower()
        if width == "u16" then return read_u16(action.address) end
        if width == "i16" or width == "s16" then return signed_u16(read_u16(action.address)) end
        if width == "u32" then return read_u32(action.address) end
        return read_u8(action.address)
    end

    local function condition_met(action)
        local actual = condition_value(action)
        local expected = action.value
        if action.operation == "ne" then return actual ~= expected end
        if action.operation == "lt" then return actual < expected end
        if action.operation == "le" then return actual <= expected end
        if action.operation == "gt" then return actual > expected end
        if action.operation == "ge" then return actual >= expected end
        return actual == expected
    end

    local experiment = {}

    function experiment.has_actions()
        return #actions > 0
    end

    function experiment.action_spec()
        return spec
    end

    function experiment.tick()
        while action_index <= #actions do
            local action = actions[action_index]
            if action.kind == "schedule" then
                local token = action.tokens[action.position]
                if token then
                    action.position = action.position + 1
                    press_token(token)
                    return token
                end
                action_index = action_index + 1
            elseif action.kind == "input" then
                if action.remaining > 0 then
                    action.remaining = action.remaining - 1
                    press_token(action.token)
                    return action.token
                end
                action_index = action_index + 1
            elseif action.kind == "memory" or action.kind == "pc" then
                if condition_met(action) then
                    action_index = action_index + 1
                elseif action.remaining <= 0 then
                    write_record({
                        { "type", json_string("experiment_wait_timeout") },
                        { "frame", tostring(current_frame()) },
                        { "kind", json_string(action.kind) },
                        { "address", tostring(action.address) },
                        { "value", tostring(action.value) }
                    })
                    action_index = action_index + 1
                else
                    action.remaining = action.remaining - 1
                    return "none"
                end
            elseif action.kind == "marker" then
                write_record({
                    { "type", json_string("marker") },
                    { "frame", tostring(current_frame()) },
                    { "name", json_string(action.name) }
                })
                if write_state then
                    write_state({
                        { "type", json_string("marker") },
                        { "format", json_string("openaladdin-frame-state-v1") },
                        { "frame", tostring(current_frame()) },
                        { "name", json_string(action.name) }
                    })
                end
                action_index = action_index + 1
            else
                action_index = action_index + 1
            end
        end
        return "none"
    end

    return experiment
end
