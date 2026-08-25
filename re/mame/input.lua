-- Controller discovery and deterministic button schedules.

return function(options)
    local core = options.core
    local machine = core.machine
    local json_string = core.json_string
    local current_frame = options.current_frame
    local root = options.root or "."

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

    local experiment = dofile(root .. "/re/mame/experiment.lua")({
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

    function input.action_spec()
        return experiment.action_spec()
    end

    return input
end
