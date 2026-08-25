-- Optional memory watches and debugger hooks for a trace run.

return function(options)
    local core = options.core
    local space = options.space
    local cpu = options.cpu
    local symbol = options.symbol
    local json_string = core.json_string
    local write_record = options.write_record
    local read_u8 = options.read_u8
    local read_u32 = options.read_u32
    local read_register = options.read_register
    local current_frame = options.current_frame

    local watched_addresses = {}
    local taps = {}

    local function parse_hex_address(value)
        local digits = value:gsub("^%s+", ""):gsub("%s+$", ""):gsub("^0[xX]", "")
        return tonumber(digits, 16)
    end

    local function add_address(address)
        watched_addresses[#watched_addresses + 1] = address
    end

    local function add_write_tap(address, name, callback, size)
        local tap_start = address & 0xfffffe
        taps[#taps + 1] = space:install_write_tap(
            tap_start,
            tap_start + (size or 2) - 1,
            name,
            callback)
    end

    local watch_list = os.getenv("OPENALADDIN_WATCH_ADDRESSES") or ""
    local debugger_watch = os.getenv("OPENALADDIN_DEBUG_WATCH") == "1"
    local breakpoint_list = os.getenv("OPENALADDIN_BREAKPOINTS") or ""
    local trace_scene_states = options.trace_scene_states
    local trace_actors = options.trace_actors
    local actor_table_base = options.actor_table_base
    local actor_stride = options.actor_stride
    local actor_slot_count = options.actor_slot_count
    local actor_type_offset = options.actor_type_offset
    local actor_animation_pc_offset = options.actor_animation_pc_offset
    local scene_state_address = options.scene_state_address
    local get_scene_state_last = options.get_scene_state_last
    local set_scene_state_last = options.set_scene_state_last

    for item in watch_list:gmatch("[^,]+") do
        local address = parse_hex_address(item)
        if address then
            add_address(address)
            local tap_start = address & 0xfffffe
            add_write_tap(
                address,
                string.format("openaladdin_watch_%06X", address),
                function(offset, data, mem_mask)
                    write_record({
                        { "type", json_string("write") },
                        { "frame", tostring(current_frame()) },
                        { "address", tostring(offset) },
                        { "data", tostring(data) },
                        { "mask", tostring(mem_mask) },
                        { "pc", tostring(read_register("PC") or 0) }
                    })
                end)

            if debugger_watch then
                local action = "printf \"OPENALADDIN_WRITE PC=%08X ADDR=%08X DATA=%08X FRAME=%08X\\n\",pc,wpaddr,wpdata,frame ; g"
                cpu.debug:wpset(space, "w", tap_start, 2, "", action)
            end
        end
    end

    for item in breakpoint_list:gmatch("[^,]+") do
        local address = parse_hex_address(item)
        if address then
            local action = "printf \"OPENALADDIN_BREAK PC=%08X FRAME=%08X\\n\",pc,frame ; g"
            cpu.debug:bpset(address, "", action)
        end
    end

    if trace_scene_states then
        add_address(scene_state_address)
        add_write_tap(
            scene_state_address,
            "openaladdin_scene_state",
            function(offset, data, mem_mask)
                local value = read_u8(scene_state_address)
                local previous = get_scene_state_last()
                if value ~= previous then
                    write_record({
                        { "type", json_string("scene_state") },
                        { "frame", tostring(current_frame()) },
                        { "address", tostring(scene_state_address) },
                        { "previous", tostring(previous) },
                        { "value", tostring(value) },
                        { "data", tostring(data) },
                        { "mask", tostring(mem_mask) },
                        { "pc", tostring(read_register("PC") or 0) },
                        { "reason", json_string("write") }
                    })
                    set_scene_state_last(value)
                end
            end)
    end

    if trace_actors then
        for slot = 0, actor_slot_count - 1 do
            local actor_slot = slot
            local record = actor_table_base + actor_slot * actor_stride
            local type_address = record + actor_type_offset
            local animation_pc_address = record + actor_animation_pc_offset

            add_address(animation_pc_address)
            add_write_tap(
                animation_pc_address,
                string.format("openaladdin_actor_%02d_animation_pc", actor_slot),
                function(offset, data, mem_mask)
                    write_record({
                        { "type", json_string("actor_write") },
                        { "frame", tostring(current_frame()) },
                        { "slot", tostring(actor_slot) },
                        { "field", json_string("animation_pc") },
                        { "record", tostring(record) },
                        { "address", tostring(offset) },
                        { "data", tostring(data) },
                        { "mask", tostring(mem_mask) },
                        { "value", tostring(read_u32(animation_pc_address)) },
                        { "actor_type", tostring(read_u8(type_address)) },
                        { "active", tostring(read_u8(type_address)) },
                        { "pc", tostring(read_register("PC") or 0) }
                    })
                end,
                4)

            add_address(type_address)
            add_write_tap(
                type_address,
                string.format("openaladdin_actor_%02d_type", actor_slot),
                function(offset, data, mem_mask)
                    write_record({
                        { "type", json_string("actor_write") },
                        { "frame", tostring(current_frame()) },
                        { "slot", tostring(actor_slot) },
                        { "field", json_string("type") },
                        { "record", tostring(record) },
                        { "address", tostring(offset) },
                        { "data", tostring(data) },
                        { "mask", tostring(mem_mask) },
                        { "value", tostring(read_u8(type_address)) },
                        { "active", tostring(read_u8(type_address)) },
                        { "animation_pc", tostring(read_u32(animation_pc_address)) },
                        { "pc", tostring(read_register("PC") or 0) }
                    })
                end)

            if debugger_watch then
                local animation_action = string.format(
                    "printf \"OPENALADDIN_ACTOR_PC SLOT=%d ADDR=%%08X DATA=%%08X PC=%%08X\\n\",wpaddr,wpdata,pc ; g",
                    actor_slot)
                cpu.debug:wpset(space, "w", animation_pc_address, 4, "", animation_action)
            end
        end
    end

    if options.trace_actor_initializers then
        local initializer_action =
            "printf \"OPENALADDIN_ACTOR_INIT DEST=%08X SOURCE=%08X A2=%08X PC=%08X RETURN=%08X\\n\",a5,a6,a2,pc,d@sp ; g"
        cpu.debug:bpset(symbol("Actor_InitializeFromTemplate"), "", initializer_action)
    end

    if options.trace_rnc_loads then
        local rnc_loader_action =
            "printf \"OPENALADDIN_RNC_LOAD PC=%08X RETURN=%08X SOURCE=%08X DEST=%08X FRAME=%08X\\n\",pc,d@sp,a0,a1,frame ; g"
        cpu.debug:bpset(symbol("RNC_To_VDP_Loader"), "", rnc_loader_action)
    end

    return {
        watched_addresses = watched_addresses,
        taps = taps
    }
end
