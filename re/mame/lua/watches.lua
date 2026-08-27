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

    local function parse_integer(value)
        local trimmed = value:gsub("^%s+", ""):gsub("%s+$", "")
        if trimmed:match("^0[xX]") then
            return tonumber(trimmed:sub(3), 16)
        end
        return tonumber(trimmed)
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
    local breakpoint_registers = os.getenv("OPENALADDIN_BREAKPOINT_REGISTERS") == "1"
    local breakpoint_row_context = os.getenv("OPENALADDIN_BREAKPOINT_ROW_CONTEXT") == "1"
    local breakpoint_terrain_context = os.getenv("OPENALADDIN_BREAKPOINT_TERRAIN_CONTEXT") == "1"
    local breakpoint_scene_context = os.getenv("OPENALADDIN_BREAKPOINT_SCENE_CONTEXT") == "1"
    local breakpoint_write_commands = {}
    local trace_audio_commands = options.trace_audio_commands
    local trace_audio_mailbox = options.trace_audio_mailbox
    local trace_audio_mailbox_reads = options.trace_audio_mailbox_reads
    local trace_scheduler = options.trace_scheduler
    local trace_scene_states = options.trace_scene_states
    local trace_selector = options.trace_selector
    local trace_actors = options.trace_actors
    local actor_table_base = options.actor_table_base
    local actor_stride = options.actor_stride
    local actor_slot_count = options.actor_slot_count
    local actor_type_offset = options.actor_type_offset
    local actor_animation_pc_offset = options.actor_animation_pc_offset
    local actor_flags_offset = options.actor_flags_offset
    local scene_state_address = options.scene_state_address
    local get_scene_state_last = options.get_scene_state_last
    local set_scene_state_last = options.set_scene_state_last

    -- Optional debugger-side writes run at each configured execution
    -- breakpoint, after the breakpoint instruction has been reached and
    -- before the action continues. This is useful for targeted RE probes
    -- where a camera refill reconstructs a row/table slot after the normal
    -- frame-boundary poke has already run.
    local breakpoint_write_spec = os.getenv("OPENALADDIN_BREAKPOINT_WRITES") or ""
    for item in breakpoint_write_spec:gmatch("[^,]+") do
        local address_text, rhs = item:match("^%s*([^=]+)%s*=%s*(.-)%s*$")
        if address_text and rhs then
            local value_text, width = rhs:match("^%s*([^:]+)%s*:%s*([uU]%d+)%s*$")
            if width then
                width = width:lower()
            else
                value_text = rhs
                width = "u8"
            end
            local address = parse_hex_address(address_text)
            local value = parse_integer(value_text)
            if address and value and (width == "u8" or width == "u16" or width == "u32") then
                local operator = width == "u8" and "b" or (width == "u16" and "w" or "d")
                breakpoint_write_commands[#breakpoint_write_commands + 1] = string.format(
                    ":maincpu.%s@$%06X=%X",
                    operator,
                    address,
                    value & (width == "u8" and 0xff or (width == "u16" and 0xffff or 0xffffffff)))
            end
        end
    end

    local function append_breakpoint_writes(action)
        if #breakpoint_write_commands == 0 then
            return action
        end
        local continue_suffix = " ; g"
        if action:sub(-#continue_suffix) == continue_suffix then
            action = action:sub(1, #action - #continue_suffix)
                .. "; " .. table.concat(breakpoint_write_commands, "; ")
                .. continue_suffix
        end
        return action
    end

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
                        { "value", tostring(read_u8(address)) },
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
            local action
            if breakpoint_registers then
                if breakpoint_terrain_context then
                    action = "printf \"OPENALADDIN_BREAK_TERRAIN PC=%08X FRAME=%08X D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X WORLDX=%04X WORLDY=%04X CAMX=%04X CAMY=%04X MAPWORD=%04X TERRAIN_INDEX=%04X FLOOR_BYTE=%02X CONTOUR_INDEX=%04X CONTOUR=%02X BEHAVIOR=%02X QUERY=%02X PUSH_R=%02X PUSH_L=%02X PUSH_U=%02X PUSH_D=%02X STATE_A=%02X STATE_B=%02X SURFACE=%04X LANDING=%02X RESPONSE=%02X\n\",pc,frame,d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,:maincpu.w@$FF7E02,:maincpu.w@$FF7E04,:maincpu.w@$FF7DF6,:maincpu.w@$FF7DF8,:maincpu.w@(a0),d3&0xffff,:maincpu.b@(a1+d3),d5&0xffff,:maincpu.b@(0x2FD2+d5),:maincpu.b@$FFF0C3,:maincpu.b@$FFF156,:maincpu.b@$FFF07C,:maincpu.b@$FFF07D,:maincpu.b@$FFF07E,:maincpu.b@$FFF07F,:maincpu.b@$FFF0CE,:maincpu.b@$FFF0CF,:maincpu.w@$FFF0A4,:maincpu.b@$FFF0C1,:maincpu.b@$FFF0BE ; g"
                elseif breakpoint_scene_context then
                    action = "printf \"OPENALADDIN_BREAK_SCENE PC=%08X FRAME=%08X D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X STATE=%02X CURSOR=%08X DATA=%08X TABLE=%04X PENDING=%02X COUNTDOWN=%02X GATE=%02X F005=%02X F0F1=%02X F14E=%04X F57D=%02X F57E=%02X F57F=%02X\\n\",pc,frame,d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,:maincpu.b@$FF7E26,:maincpu.d@$FFF572,:maincpu.d@$FFF576,:maincpu.w@$FFF57A,:maincpu.b@$FFF57C,:maincpu.b@$FFF0E9,:maincpu.b@$FFF176,:maincpu.b@$FFF005,:maincpu.b@$FFF0F1,:maincpu.w@$FFF14E,:maincpu.b@$FFF57D,:maincpu.b@$FFF57E,:maincpu.b@$FFF57F ; g"
                elseif breakpoint_row_context then
                    action = "printf \"OPENALADDIN_BREAK_ROW PC=%08X FRAME=%08X D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X ROWWORD=%04X ROWINDEX=%04X INTERACTION=%02X HANDLER=%08X F003=%02X F57D=%02X F7E49=%02X F0D8=%02X A1TYPE=%02X A1X=%04X A1FLAGS=%02X\\n\",pc,frame,d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,:maincpu.w@(a0-2),d2&0xffff,d3&0xff,a4,:maincpu.b@$FFF003,:maincpu.b@$FFF57D,:maincpu.b@$FF7E49,:maincpu.b@$FFF0D8,:maincpu.b@a1,:maincpu.w@(a1+2),:maincpu.b@(a1+0x3C) ; g"
                else
                    action = "printf \"OPENALADDIN_BREAK_REGS PC=%08X FRAME=%08X D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X F003=%02X F57D=%02X F7E49=%02X F0D8=%02X A1TYPE=%02X A1X=%04X A1FLAGS=%02X\\n\",pc,frame,d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,:maincpu.b@$FFF003,:maincpu.b@$FFF57D,:maincpu.b@$FF7E49,:maincpu.b@$FFF0D8,:maincpu.b@a1,:maincpu.w@(a1+2),:maincpu.b@(a1+0x3C) ; g"
                end
            else
                action = "printf \"OPENALADDIN_BREAK PC=%08X FRAME=%08X\\n\",pc,frame ; g"
            end
            cpu.debug:bpset(address, "", append_breakpoint_writes(action))
        end
    end

    if trace_audio_commands then
        if not cpu.debug then
            error("OPENALADDIN_TRACE_AUDIO_COMMANDS requires MAME debugger support")
        end

        -- At 0x1AC9D8 the shared F3 handler has loaded its byte operand into
        -- D0. A2 is the stream cursor after that operand and A1 is the actor
        -- record receiving the animation command.
        cpu.debug:bpset(
            0x001AC9D8,
            "",
            "printf \"OPENALADDIN_AUDIO_COMMAND KIND=SFX ID=%08X PC=%08X FRAME=%08X ACTOR=%08X STREAM=%08X SCENE_FLAG=%02X\\n\",d0,pc,frame,a1,a2,:maincpu.b@$FFF57D ; g")

        -- The level music selector has loaded the level-table word at +0x1A
        -- into D0 at this instruction, before checking the transition gate.
        cpu.debug:bpset(
            0x001AE1F2,
            "",
            "printf \"OPENALADDIN_AUDIO_COMMAND KIND=MUSIC ID=%08X PC=%08X FRAME=%08X LEVEL=%02X SCENE_FLAG=%02X\\n\",d0,pc,frame,:maincpu.b@$FF7E26,:maincpu.b@$FFF57F ; g")

        -- Player interaction setup queues the fixed sound/event 0x31 through
        -- the same shared audio dispatch path.
        cpu.debug:bpset(
            0x001AE5AA,
            "",
            "printf \"OPENALADDIN_AUDIO_COMMAND KIND=EVENT ID=00000031 PC=%08X FRAME=%08X SCENE_FLAG=%02X\\n\",pc,frame,:maincpu.b@$FFF57D ; g")

        -- These are the two common routines reached by the level-music and
        -- animation F3 paths. Their register state tells us whether the ROM
        -- command was converted into a Z80 mailbox operation.
        cpu.debug:bpset(
            0x001E58B8,
            "",
            "printf \"OPENALADDIN_AUDIO_DISPATCH KIND=PREP PC=%08X FRAME=%08X D0=%08X D1=%08X A0=%08X A1=%08X A2=%08X\\n\",pc,frame,d0,d1,a0,a1,a2 ; g")
        cpu.debug:bpset(
            0x001E589A,
            "",
            "printf \"OPENALADDIN_AUDIO_DISPATCH KIND=SEND PC=%08X FRAME=%08X D0=%08X D1=%08X A0=%08X A1=%08X A2=%08X\\n\",pc,frame,d0,d1,a0,a1,a2 ; g")
    end

    if trace_audio_mailbox then
        if not cpu.debug then
            error("OPENALADDIN_TRACE_AUDIO_MAILBOX requires MAME debugger support")
        end

        -- The common send routine presents the Z80 shared-RAM command cell at
        -- A0 == $A00036. A debugger watchpoint observes the write without
        -- replacing the Genesis Z80-RAM handler (a broad Lua memory tap would
        -- prevent the boot-time Z80 program upload in this MAME driver).
        cpu.debug:wpset(
            space,
            "w",
            0x00A00036,
            2,
            "frame > 10",
            "printf \"OPENALADDIN_AUDIO_MAILBOX ADDR=%08X DATA=%08X PC=%08X FRAME=%08X D0=%08X A0=%08X\\n\",wpaddr,wpdata,pc,frame,d0,a0 ; g")

        -- The cursor above indexes the 64-byte sound command queue at
        -- $A01B40. The command helpers write a marker and one or more
        -- operands into that queue before publishing the updated cursor.
        -- Watch the queue itself so the native decoder can recover packet
        -- boundaries instead of inferring them from cursor changes alone.
        cpu.debug:wpset(
            space,
            "w",
            0x00A01B40,
            0x40,
            "frame > 10",
            "printf \"OPENALADDIN_AUDIO_MAILBOX_DATA ADDR=%08X DATA=%08X PC=%08X FRAME=%08X D0=%08X D1=%08X A0=%08X A1=%08X\\n\",wpaddr,wpdata,pc,frame,d0,d1,a0,a1 ; g")

        if trace_audio_mailbox_reads then
            local read_frames = os.getenv("OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES") or ""
            if read_frames == "" then
                error("OPENALADDIN_TRACE_AUDIO_MAILBOX_READS requires OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES")
            end
            local conditions = {}
            for item in read_frames:gmatch("[^,]+") do
                local value = parse_hex_address(item)
                if value then
                    -- MAME debugger numeric literals are hexadecimal.
                    conditions[#conditions + 1] = string.format("frame == %X", value)
                end
            end
            if #conditions == 0 then
                error("OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES contains no valid frame values")
            end

            -- On the Z80 side the 68K address $A00036 is the adjacent byte in
            -- the shared program-RAM view. Restrict reads to selected command
            -- frames; the sound driver polls this cell continuously.
            local z80 = core.machine.devices[":genesis_snd_z80"]
            local z80_space = z80 and z80.spaces["program"] or nil
            if z80 and z80.debug and z80_space then
                z80.debug:wpset(
                    z80_space,
                    "r",
                    0x0030,
                    0x10,
                    table.concat(conditions, " || "),
                    "printf \"OPENALADDIN_AUDIO_MAILBOX_READ ADDR=%08X DATA=%08X VISIBLE_PC=%08X FRAME=%08X\\n\",wpaddr,wpdata,pc,frame ; g")
            else
                error("OPENALADDIN_TRACE_AUDIO_MAILBOX_READS requires a debuggable Genesis Z80")
            end
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
            local flags_address = record + actor_flags_offset

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

            add_address(flags_address)
            add_write_tap(
                flags_address,
                string.format("openaladdin_actor_%02d_flags", actor_slot),
                function(offset, data, mem_mask)
                    local value = read_u8(flags_address)
                    write_record({
                        { "type", json_string("actor_write") },
                        { "frame", tostring(current_frame()) },
                        { "slot", tostring(actor_slot) },
                        { "field", json_string("flags") },
                        { "record", tostring(record) },
                        { "address", tostring(offset) },
                        { "data", tostring(data) },
                        { "mask", tostring(mem_mask) },
                        { "value", tostring(value) },
                        { "flag_bit5", tostring((value & 0x20) ~= 0) },
                        { "actor_type", tostring(read_u8(type_address)) },
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

    if trace_scheduler then
        if not cpu.debug then
            error("OPENALADDIN_TRACE_SCHEDULER requires MAME debugger support")
        end

        local function scheduler_watch(address, size)
            local watch_address = address & 0xfffffe
            cpu.debug:wpset(
                space,
                "w",
                watch_address,
                size or 2,
                "",
                "printf \"OPENALADDIN_SCHEDULER_WRITE PC=%08X FRAME=%08X ADDR=%08X DATA=%08X\\n\",pc,frame,wpaddr,wpdata ; g")
        end

        local scheduler_symbols = {
            "PLAYER_X", "PLAYER_Y", "PLAYER_VX", "PLAYER_VY",
            "PLAYER_FRAME_PTR", "PLAYER_ANIMATION_PC", "PLAYER_ANIMATION_TIMER",
            "PLAYER_ACTOR_FLAGS", "PLAYER_INTERACTION_PENDING", "SCENE_STATE",
            "SCENE_SCRIPT_COUNTDOWN", "TERRAIN_LANDING_STATE",
            "TERRAIN_RESPONSE_ACTIVE", "TERRAIN_VERTICAL_STOP",
            "TERRAIN_RESPONSE_TIMER_STATE", "TERRAIN_QUERY_STATE_A",
            "TERRAIN_QUERY_STATE_B", "TERRAIN_STATE", "TERRAIN_RESPONSE_LATCH",
            "WORLD_CAMERA_X", "WORLD_CAMERA_Y", "CAMERA_REFERENCE_X",
            "CAMERA_REFERENCE_Y", "CAMERA_SCROLL_X", "CAMERA_SCROLL_Y",
            "CAMERA_UPDATE_DELAY", "CAMERA_SPECIAL_MODE"
        }
        for _, name in ipairs(scheduler_symbols) do
            scheduler_watch(symbol(name))
        end

        for slot = 0, math.min(actor_slot_count, 32) - 1 do
            local record = actor_table_base + slot * actor_stride
            -- One range watch per record preserves slot coverage while
            -- avoiding hundreds of overlapping debugger watchpoints. The
            -- debugger reports the exact byte/word address that triggered it.
            scheduler_watch(record, actor_stride)
        end
    end

    if options.trace_actor_initializers then
        local initializer_action =
            "printf \"OPENALADDIN_ACTOR_INIT DEST=%08X SOURCE=%08X A2=%08X PC=%08X RETURN=%08X FRAME=%08X\\n\",a5,a6,a2,pc,d@sp,frame ; g"
        cpu.debug:bpset(symbol("Actor_InitializeFromTemplate"), "", initializer_action)
    end

    if options.trace_rnc_loads then
        local rnc_loader_action =
            "printf \"OPENALADDIN_RNC_LOAD PC=%08X RETURN=%08X SOURCE=%08X DEST=%08X FRAME=%08X\\n\",pc,d@sp,a0,a1,frame ; g"
        cpu.debug:bpset(symbol("RNC_To_VDP_Loader"), "", rnc_loader_action)
    end

    if trace_selector then
        local selector_action = string.format(
            "printf \"OPENALADDIN_SELECTOR FRAME=%%08X PC=%%08X E7=%%02X E6=%%02X E9=%%02X F2=%%02X BE=%%02X C1=%%02X D0=%%02X D7=%%02X CD=%%02X D4=%%02X F173=%%02X CC=%%02X EFFF=%%02X F11F=%%02X D8=%%02X RET=%%08X ANIMPC=%%08X TIMER=%%02X VX=%%04X\\n\",frame,pc,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,:maincpu.b@$%06X,d@sp,:maincpu.d@$%06X,:maincpu.b@$%06X,:maincpu.w@$%06X ; g",
            symbol("PLAYER_INTERACTION_ANIMATION_GATE"),
            symbol("PLAYER_TERMINAL_TRANSITION"),
            symbol("SCENE_SCRIPT_COUNTDOWN"),
            symbol("PLAYER_INTERACTION_LOCK"),
            symbol("TERRAIN_RESPONSE_ACTIVE"),
            symbol("TERRAIN_LANDING_STATE"),
            symbol("PLAYER_TRANSITION_GATE"),
            symbol("PLAYER_INTERACTION_TRANSITION_LOCK"),
            symbol("PLAYER_INTERACTION_MODE"),
            symbol("PLAYER_INTERACTION_RESPONSE"),
            symbol("CAMERA_SPECIAL_MODE"),
            symbol("TERRAIN_RESPONSE_TIMER_STATE"),
            symbol("PLAYER_INTERACTION_PENDING"),
            symbol("PLAYER_INTERACTION_STATE_LOCK"),
            0xFFF0D8,
            symbol("PLAYER_ANIMATION_PC"),
            symbol("PLAYER_ANIMATION_TIMER"),
            symbol("PLAYER_VX"))
        cpu.debug:bpset(symbol("Player_ProcessInteractionState"), "", selector_action)
    end

    return {
        watched_addresses = watched_addresses,
        taps = taps
    }
end
