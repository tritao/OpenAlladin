-- Controller discovery and deterministic button schedules.

return function(options)
    local core = options.core
    local machine = core.machine
    local json_string = core.json_string
    local json_array = core.json_array
    local json_object = core.json_object
    local current_frame = options.current_frame
    local root = options.root or "."
    local mode = (os.getenv("OPENALADDIN_INPUT_MODE") or "inject"):lower()
    if mode ~= "inject" and mode ~= "record" and mode ~= "playback" then
        error("OPENALADDIN_INPUT_MODE must be inject, record, or playback")
    end
    local write_input = options.write_input

    local function split(value, separator)
        local result = {}
        for item in (value .. separator):gmatch("(.-)" .. separator) do
            result[#result + 1] = item
        end
        return result
    end

    local fields = {}
    local field_names = {}
    local controller_port = machine.ioport.ports[":ctrl1:mdpad:PAD"]
    for port_tag, port in pairs(machine.ioport.ports) do
        for field_name, field in pairs(port.fields) do
            local normalized = field_name:lower()
            if normalized:find("p1", 1, true)
                or normalized:find("player 1", 1, true)
                or normalized:find("1p", 1, true) then
                fields[#fields + 1] = {
                    name = field_name,
                    normalized = normalized,
                    field = field,
                    port = port_tag
                }
                field_names[#field_names + 1] = json_string(field_name)
            end
        end
    end
    table.sort(fields, function(left, right) return left.name < right.name end)
    table.sort(field_names)

    local function find_field(words)
        for _, entry in ipairs(fields) do
            local matches = true
            for _, word in ipairs(words) do
                if not entry.normalized:find(word, 1, true) then
                    matches = false
                    break
                end
            end
            if matches then return entry.field end
        end
        return nil
    end

    local function find_field_any(candidates)
        for _, words in ipairs(candidates) do
            local field = find_field(words)
            if field then return field end
        end
        return nil
    end

    local controls = {
        up = find_field({ "up" }),
        down = find_field({ "down" }),
        left = find_field({ "left" }),
        right = find_field({ "right" }),
        a = find_field_any({ { "button 1" }, { "p1 a" }, { "player 1 a" } }),
        b = find_field_any({ { "button 2" }, { "p1 b" }, { "player 1 b" } }),
        c = find_field_any({ { "button 3" }, { "p1 c" }, { "player 1 c" } }),
        start = find_field({ "start" })
    }

    -- MAME exposes the Mega Drive pad as an active-low eight-bit port.  The
    -- input-run format deliberately exposes the same logical order as the
    -- Genesis controller, but with active-high bits so it is readable and
    -- portable across clients.
    local button_order = { "up", "down", "left", "right", "a", "b", "c", "start" }
    local button_masks = {}
    for index, name in ipairs(button_order) do
        button_masks[name] = 1 << (index - 1)
    end

    -- The Mega Drive input port is not laid out in canonical button order:
    -- B is bit 4, C is bit 5, and A is bit 6. Keep the portable timeline in
    -- Genesis order while deriving the physical masks from MAME's fields.
    local port_masks = {}
    for _, name in ipairs(button_order) do
        local field = controls[name]
        port_masks[name] = field and field.mask or button_masks[name]
    end

    local function canonical_mask()
        local raw = controller_port and controller_port:read() or 0xff
        local mask = 0
        for _, name in ipairs(button_order) do
            local port_mask = port_masks[name]
            if port_mask and (raw & port_mask) == 0 then
                mask = mask | button_masks[name]
            end
        end
        return mask
    end

    local function token_for_mask(mask)
        local pressed = {}
        for index, name in ipairs(button_order) do
            if (mask & button_masks[name]) ~= 0 then
                pressed[#pressed + 1] = name
            end
        end
        return #pressed == 0 and "none" or table.concat(pressed, "+")
    end

    if write_input then
        write_input({
            { "type", json_string("header") },
            { "format", json_string("openaladdin-input-v1") },
            { "controller_mapping", json_string("mame-genesis-3button-v1") },
            { "buttons", json_array((function ()
                local values = {}
                for index, name in ipairs(button_order) do
                    values[index] = json_string(name)
                end
                return values
            end)()) },
            { "mask_bits", json_object((function ()
                local values = {}
                for _, name in ipairs(button_order) do
                    values[#values + 1] = { name, tostring(button_masks[name]) }
                end
                return values
            end)()) },
            { "frame_semantics", json_string(
                "I[N] is the controller input used for the transition S[N] -> S[N+1] at synchronization boundary N"
            ) },
            { "frame_contract", json_string("S[N] = synchronized state at boundary N; I[N] = input for S[N] -> S[N+1]") },
            { "mode", json_string(mode) }
        })
    end

    local function expand_schedule(value)
        local result = {}
        for _, item in ipairs(split(value, ",")) do
            local token, count = item:match("^%s*(.-)%s*[*:](%d+)%s*$")
            if token then
                for _ = 1, tonumber(count) do result[#result + 1] = token:lower() end
            else
                result[#result + 1] = item:lower():gsub("^%s+", ""):gsub("%s+$", "")
            end
        end
        return result
    end

    local input = {}
    local schedule_tokens = expand_schedule(os.getenv("OPENALADDIN_INPUT") or "none")

    local function press_token(token)
        if token == "" or token == "none" then return end
        for part in token:gmatch("[^+]+") do
            local field = controls[part]
            if field then field:set_value(1) end
        end
    end

    local experiment = dofile(root .. "/re/mame/lua/experiment.lua")({
        core = core,
        write_record = options.write_record,
        write_state = options.write_state,
        current_frame = current_frame,
        press_token = press_token,
        expand_schedule = expand_schedule
    })

    local function clear()
        for _, entry in ipairs(fields) do entry.field:set_value(0) end
    end

    function input.apply()
        if mode == "record" or mode == "playback" then
            local mask = canonical_mask()
            local token = token_for_mask(mask)
            if write_input then
                write_input({
                    { "frame", tostring(current_frame()) },
                    { "mask", tostring(mask) },
                    { "buttons", json_array((function ()
                        local values = {}
                        for index, name in ipairs(button_order) do
                            if (mask & button_masks[name]) ~= 0 then
                                values[#values + 1] = json_string(name)
                            end
                        end
                        return values
                    end)()) }
                })
            end
            return token
        end

        clear()
        if experiment.has_actions() then return experiment.tick() end
        local token = schedule_tokens[current_frame() + 1] or "none"
        press_token(token)
        return token
    end

    function input.fields_json()
        return "[" .. table.concat(field_names, ",") .. "]"
    end

    function input.controller_value()
        return controller_port and controller_port:read() or 0
    end

    function input.canonical_mask()
        return canonical_mask()
    end

    function input.action_spec()
        return experiment.action_spec()
    end

    return input
end
