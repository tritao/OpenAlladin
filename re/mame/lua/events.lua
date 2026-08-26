-- Passive semantic event detectors for recorded MAME sessions.

return function(options)
    local core = options.core
    local json_string = core.json_string
    local json_array = core.json_array
    local json_object = core.json_object
    local symbol = options.symbol
    local read_u8 = options.read_u8
    local read_u16 = options.read_u16
    local read_u32 = options.read_u32
    local signed_u16 = options.signed_u16
    local read_register = options.read_register
    local write_event = options.write_event
    local write_record = options.write_record
    local write_state = options.write_state
    local current_frame = options.current_frame
    local save_checkpoint = options.save_checkpoint

    local function split(value, separator)
        local result = {}
        for item in (value .. separator):gmatch("(.-)" .. separator) do
            result[#result + 1] = item
        end
        return result
    end

    local function parse_condition(encoded)
        local fields = split(encoded, ",")
        if #fields ~= 5 then
            error("event condition must be kind,name,width,operation,value: " .. encoded)
        end
        local value = tonumber(fields[5])
        if value == nil then
            error("event condition value must be numeric: " .. encoded)
        end
        return {
            kind = fields[1],
            name = fields[2],
            width = fields[3],
            operation = fields[4],
            value = value
        }
    end

    local detectors = {}
    local spec = os.getenv("OPENALADDIN_EVENT_SPEC") or ""
    if spec ~= "" then
        for _, encoded in ipairs(split(spec, ";")) do
            if encoded ~= "" then
                local fields = split(encoded, "|")
                if #fields ~= 8 then
                    error("event detector must have 8 fields: " .. encoded)
                end
                local stable_for = tonumber(fields[6]) or 1
                if stable_for < 1 then
                    error("event detector stable_for must be positive: " .. encoded)
                end
                local conditions = {}
                for _, condition in ipairs(split(fields[8], "~")) do
                    conditions[#conditions + 1] = parse_condition(condition)
                end
                detectors[#detectors + 1] = {
                    name = fields[1],
                    event = fields[2],
                    phase = fields[3],
                    level = fields[4],
                    checkpoint = fields[5],
                    stable_for = stable_for,
                    emit = fields[7] == "0" and "every_stable_interval"
                        or (fields[7] == "1" and "once" or fields[7]),
                    conditions = conditions,
                    active_frames = 0,
                    onset_frame = nil,
                    emitted = false
                }
            end
        end
    end

    local function condition_value(condition)
        if condition.kind == "pc" then
            return read_register() or 0
        end
        if condition.kind ~= "symbol" then
            error("unsupported event condition kind: " .. condition.kind)
        end
        local address = symbol(condition.name)
        local width = condition.width:lower()
        if width == "u16" then return read_u16(address) end
        if width == "i16" or width == "s16" then return signed_u16(read_u16(address)) end
        if width == "u32" then return read_u32(address) end
        return read_u8(address)
    end

    local function condition_met(actual, operation, expected)
        if operation == "ne" then return actual ~= expected end
        if operation == "lt" then return actual < expected end
        if operation == "le" then return actual <= expected end
        if operation == "gt" then return actual > expected end
        if operation == "ge" then return actual >= expected end
        return actual == expected
    end

    local function detector_event(detector, onset_frame, confirmed_frame, evidence)
        local checkpoint = detector.checkpoint ~= "" and detector.checkpoint or detector.name
        local state_path = save_checkpoint and save_checkpoint(checkpoint) or ""
        local evidence_json = json_array(evidence)
        local event_fields = {
            { "type", json_string("event") },
            { "format", json_string("openaladdin-event-v1") },
            { "frame", tostring(confirmed_frame) },
            { "onset_frame", tostring(onset_frame) },
            { "confirmed_frame", tostring(confirmed_frame) },
            { "stable_for", tostring(detector.stable_for) },
            { "emit", json_string(detector.emit) },
            { "name", json_string(detector.name) },
            { "event", json_string(detector.event) },
            { "phase", json_string(detector.phase) },
            { "level", json_string(detector.level) },
            { "checkpoint", json_string(checkpoint) },
            { "state", json_string(state_path) },
            { "evidence", evidence_json }
        }
        if write_event then write_event(event_fields) end

        -- Preserve the existing marker format used by aligned_trace and the
        -- scripted experiment probes. The dedicated event stream carries the
        -- richer record; these mirrors keep old consumers working.
        local marker_fields = {
            { "type", json_string("marker") },
            { "frame", tostring(confirmed_frame) },
            { "onset_frame", tostring(onset_frame) },
            { "confirmed_frame", tostring(confirmed_frame) },
            { "name", json_string(detector.name) },
            { "event", json_string(detector.event) },
            { "phase", json_string(detector.phase) },
            { "level", json_string(detector.level) },
            { "checkpoint", json_string(checkpoint) },
            { "state", json_string(state_path) },
            { "evidence", evidence_json }
        }
        if write_record then write_record(marker_fields) end
        if write_state then
            local state_marker = {
                { "type", json_string("marker") },
                { "format", json_string("openaladdin-frame-state-v1") },
                { "frame", tostring(confirmed_frame) },
                { "onset_frame", tostring(onset_frame) },
                { "confirmed_frame", tostring(confirmed_frame) },
                { "name", json_string(detector.name) },
                { "event", json_string(detector.event) },
                { "phase", json_string(detector.phase) },
                { "level", json_string(detector.level) },
                { "checkpoint", json_string(checkpoint) },
                { "state", json_string(state_path) },
                { "evidence", evidence_json }
            }
            write_state(state_marker)
        end
    end

    local events = {}
    function events.has_detectors()
        return #detectors > 0
    end

    function events.detector_spec()
        return spec
    end

    function events.poll(frame)
        for _, detector in ipairs(detectors) do
            local active = true
            local evidence = {}
            for _, condition in ipairs(detector.conditions) do
                local actual = condition_value(condition)
                if not condition_met(actual, condition.operation, condition.value) then
                    active = false
                end
                evidence[#evidence + 1] = json_object({
                    { "kind", json_string(condition.kind) },
                    { "name", json_string(condition.name) },
                    { "width", json_string(condition.width) },
                    { "operation", json_string(condition.operation) },
                    { "expected", tostring(condition.value) },
                    { "actual", tostring(actual) }
                })
            end

            if active then
                if detector.active_frames == 0 then
                    detector.onset_frame = frame
                end
                detector.active_frames = detector.active_frames + 1
            else
                detector.active_frames = 0
                detector.onset_frame = nil
                if detector.emit ~= "once" then
                    detector.emitted = false
                end
            end

            if active
                and detector.active_frames >= detector.stable_for
                and not detector.emitted then
                detector_event(detector, detector.onset_frame or frame, frame, evidence)
                detector.emitted = true
            end
        end
    end

    return events
end
