-- OpenAladdin first-pass MAME trace harness.
--
-- The baseline trace remains generic, while the optional experiment protocol
-- can wait on the tracked symbols and PCs needed by named gameplay probes.
--
-- Run through tools/oa.py trace rather than invoking this file directly.

local root = os.getenv("OPENALADDIN_ROOT") or "."
local core = dofile(root .. "/re/mame/lua/core.lua")
local machine = core.machine
local cpu = core.cpu
local space = core.space
local S = core.symbols
local function symbol(name)
    local value = S[name]
    if value == nil then
        error("missing generated MAME symbol: " .. name .. "; run python tools/oa.py ghidra rebuild")
    end
    return value
end
local env_number = core.env_number
local json_string = core.json_string
local json_bool = core.json_bool
local json_array = core.json_array
local json_object = core.json_object
local read_u8 = core.read_u8
local read_u16 = core.read_u16
local read_u32 = core.read_u32
local signed_u16 = core.signed_u16
local read_register = core.read_register

local trace_dir = os.getenv("OPENALADDIN_TRACE_DIR") or "build/re/traces"
-- A negative frame limit means interactive/unbounded mode.  The normal trace
-- frontend continues to pass a non-negative limit; record mode lets the user
-- quit MAME when the session is complete.
local frame_limit = math.floor(env_number("OPENALADDIN_TRACE_FRAMES", 120))
local save_frame = math.floor(env_number("OPENALADDIN_SAVE_FRAME", -1))
local snapshot_frame = math.floor(env_number("OPENALADDIN_SNAPSHOT_FRAME", -1))
local poke_frame = math.floor(env_number("OPENALADDIN_POKE_FRAME", -1))
local preload_state = os.getenv("OPENALADDIN_PRELOAD_STATE") or ""
local save_name = os.getenv("OPENALADDIN_SAVE_NAME") or "gameplay"
local snapshot_name = os.getenv("OPENALADDIN_SNAPSHOT_NAME") or "gameplay.png"
local checkpoint_spec = os.getenv("OPENALADDIN_CHECKPOINTS") or ""
local checkpoint_reference = os.getenv("OPENALADDIN_CHECKPOINT_REFERENCE") or "states"
local checkpoints = {}
for item in checkpoint_spec:gmatch("[^,]+") do
    local frame_text, name = item:match("^%s*(%-?%d+)%s*=%s*(.-)%s*$")
    local frame = frame_text and tonumber(frame_text) or nil
    if frame and name and name ~= "" then
        checkpoints[math.floor(frame)] = name
    elseif item:match("%S") then
        error("OPENALADDIN_CHECKPOINTS entries must use frame=name: " .. item)
    end
end
local ram_start = symbol("WORK_RAM_BASE")
local ram_size = 0x10000
local requested_capture = os.getenv("OPENALADDIN_CAPTURE")
if not requested_capture then
    local legacy_vdp = os.getenv("OPENALADDIN_CAPTURE_VDP")
    requested_capture = legacy_vdp == nil and "state" or (legacy_vdp == "0" and "state" or "full")
end
local capture_profile = requested_capture:lower()
if capture_profile ~= "state" and capture_profile ~= "ram"
    and capture_profile ~= "vdp" and capture_profile ~= "full" then
    error("OPENALADDIN_CAPTURE must be state, ram, vdp, or full")
end
local capture_ram = capture_profile == "ram" or capture_profile == "full"
local capture_vdp = capture_profile == "vdp" or capture_profile == "full"
local trace_audio = os.getenv("OPENALADDIN_TRACE_AUDIO") == "1"
local trace_audio_mailbox = os.getenv("OPENALADDIN_TRACE_AUDIO_MAILBOX") == "1"
local trace_audio_mailbox_reads = os.getenv("OPENALADDIN_TRACE_AUDIO_MAILBOX_READS") == "1"
local trace_audio_commands = os.getenv("OPENALADDIN_TRACE_AUDIO_COMMANDS") == "1"
local state_sync = os.getenv("OPENALADDIN_STATE_SYNC") == "1"

local state_output = capture_profile == "state" or os.getenv("OPENALADDIN_STATE_OUTPUT") == "1"
local event_output = os.getenv("OPENALADDIN_EVENT_OUTPUT")
local event_spec = os.getenv("OPENALADDIN_EVENT_SPEC") or ""
local capture_streams = dofile(root .. "/re/mame/lua/capture.lua")({
    core = core,
    trace_dir = trace_dir,
    profile = capture_profile,
    capture_ram = capture_ram,
    capture_vdp = capture_vdp,
    trace_audio = trace_audio,
    state_output = state_output,
    input_output = os.getenv("OPENALADDIN_INPUT_OUTPUT"),
    event_output = event_output,
    event_spec = event_spec,
    ram_start = ram_start,
    ram_size = ram_size,
    read_u8 = read_u8
})
local vdp_vram = capture_streams.vdp_vram
local vdp_cram = capture_streams.vdp_cram
local vdp_vsram = capture_streams.vdp_vsram
local vdp_regs = capture_streams.vdp_regs
local vdp_writes = capture_streams.vdp_writes
local state = capture_streams.state
local write_record = capture_streams.write_record
local write_state = capture_streams.write_state
local write_event = capture_streams.write_event

local trace_scene_states = os.getenv("OPENALADDIN_TRACE_SCENE_STATES") == "1"
local trace_selector = os.getenv("OPENALADDIN_TRACE_SELECTOR") == "1"
local scene_state_address = symbol("SCENE_STATE")
local scene_state_last = read_u8(scene_state_address)
local preload_applied = false
local trace_actors = os.getenv("OPENALADDIN_TRACE_ACTORS") == "1"
local actor_table_base = symbol("ACTOR_TABLE_BASE")
local actor_stride = 0x42
local actor_slot_count = math.max(0, math.floor(env_number("OPENALADDIN_ACTOR_SLOTS", 32)))
local actor_type_offset = 0x00
local actor_x_offset = 0x02
local actor_y_offset = 0x04
local actor_facing_x_flip_offset = 0x09
local actor_movement_pc_offset = 0x0a
local actor_movement_loop_pc_offset = 0x0e
local actor_movement_loop_timer_offset = 0x12
local actor_frame_ptr_offset = 0x14
local actor_animation_pc_offset = 0x20
local actor_facing_y_flip_offset = 0x35
local actor_movement_command_timer_offset = 0x36
local actor_animation_timer_offset = 0x37
local actor_movement_return_pc_offset = 0x38
local actor_flags_offset = 0x3c
local current_frame = 0

local function signed_u8(value)
    value = value & 0xff
    return value >= 0x80 and value - 0x100 or value
end

-- FUN_001ABB40 reads the current animation record's bounds at +2..+5.
-- The normal path uses the raw byte offsets; the X-flipped path mirrors the
-- signed X offsets around the actor origin. Keep this in the MAME trace so
-- native comparisons can validate the resolved geometry, not just pointers.
local function collision_box_json(frame_pointer, origin_x, origin_y, facing_left)
    if frame_pointer == 0 then
        return "null"
    end
    local left
    local right
    if facing_left then
        left = origin_x - signed_u8(read_u8(frame_pointer + 4))
        right = origin_x - signed_u8(read_u8(frame_pointer + 2))
    else
        left = origin_x + read_u8(frame_pointer + 2)
        right = origin_x + read_u8(frame_pointer + 4)
    end
    local top = origin_y + read_u8(frame_pointer + 3)
    local bottom = origin_y + read_u8(frame_pointer + 5)
    return json_object({
        { "left", tostring(left) },
        { "top", tostring(top) },
        { "right", tostring(right) },
        { "bottom", tostring(bottom) }
    })
end

local vdp_device = core.find_device(":gen_vdp", "sega315_5313")
local vdp = dofile(root .. "/re/mame/lua/vdp.lua")({
    core = core,
    capture = capture_vdp,
    vram = core.find_save_item(vdp_device, "m_vram"),
    cram = core.find_save_item(vdp_device, "m_cram"),
    vsram = core.find_save_item(vdp_device, "m_vsram"),
    regs = core.find_save_item(vdp_device, "m_regs"),
    address = core.find_save_item(vdp_device, "m_vdp_address"),
    code = core.find_save_item(vdp_device, "m_vdp_code"),
    command_pending = core.find_save_item(vdp_device, "m_command_pending"),
    writes = vdp_writes,
    current_frame = function () return current_frame end,
    read_register = read_register
})
vdp.set_outputs({
    vram = vdp_vram,
    cram = vdp_cram,
    vsram = vdp_vsram,
    regs = vdp_regs
})

local audio = dofile(root .. "/re/mame/lua/audio.lua")({
    core = core,
    main_space = space,
    main_cpu = cpu,
    writes = capture_streams.sound_writes,
    trace_dir = trace_dir,
    current_frame = function () return current_frame end
})

local function vdp_state_json()
    return vdp.state_json()
end

local function dump_vdp()
    vdp.dump()
end

local function vdp_items_json()
    return vdp.items_json()
end

local vdp_taps = vdp.install_taps(space, symbol("VDP_DATA"))

local edge_tracer = dofile(root .. "/re/mame/lua/edges.lua")({
    cpu = cpu,
    symbol = symbol,
    read_u32 = read_u32
})

local function scene_runtime_json()
    return json_object({
        { "state", tostring(read_u8(symbol("SCENE_STATE"))) },
        { "script_cursor", tostring(read_u32(symbol("SCENE_SCRIPT_CURSOR"))) },
        { "script_data_cursor", tostring(read_u32(symbol("SCENE_SCRIPT_DATA"))) },
        { "table_index", tostring(read_u16(symbol("SCENE_TABLE_INDEX"))) },
        { "script_pending", tostring(read_u8(symbol("SCENE_SCRIPT_PENDING"))) },
        { "vdp_update", tostring(read_u8(symbol("SCENE_VDP_UPDATE_FLAG"))) },
        { "vdp_clear", tostring(read_u8(symbol("SCENE_VDP_CLEAR_FLAG"))) },
        { "transition_event", tostring(read_u8(symbol("SCENE_TRANSITION_EVENT"))) },
        { "script_countdown", tostring(read_u8(symbol("SCENE_SCRIPT_COUNTDOWN"))) },
        { "script_gate", tostring(read_u8(symbol("SCENE_SCRIPT_GATE"))) },
        { "player_gate", tostring(read_u8(symbol("PLAYER_TRANSITION_GATE"))) },
        { "player_lock", tostring(read_u8(symbol("PLAYER_TRANSITION_LOCK"))) },
        { "player_countdown", tostring(read_u8(symbol("PLAYER_TRANSITION_COUNTDOWN"))) },
        { "player_terminal", tostring(read_u8(symbol("PLAYER_TERMINAL_TRANSITION"))) }
    })
end

local function terrain_runtime_json()
    local world_x = read_u16(symbol("PLAYER_WORLD_X"))
    local world_y = read_u16(symbol("PLAYER_WORLD_Y"))
    local collision_y = world_y - 0x110
    return json_object({
        { "world_x", tostring(world_x) },
        { "world_y", tostring(world_y) },
        { "collision_probe_row", tostring(math.floor(collision_y / 16)) },
        { "collision_probe_column", tostring(world_x >> 4) },
        { "collision_probe_right_base_column", tostring((world_x >> 4) + 2) },
        { "collision_probe_ceiling_column", tostring((world_x >> 4) + 1) },
        { "collision_probe_landing_state", tostring(read_u8(symbol("TERRAIN_LANDING_STATE"))) },
        { "query_callback_a", tostring(read_u32(symbol("TERRAIN_QUERY_CALLBACK_A"))) },
        { "query_callback_b", tostring(read_u32(symbol("TERRAIN_QUERY_CALLBACK_B"))) },
        { "query_callback_c", tostring(read_u32(symbol("TERRAIN_QUERY_CALLBACK_C"))) },
        { "query_result", tostring(read_u8(symbol("TERRAIN_QUERY_FLAGS"))) },
        { "push_right", tostring(read_u8(symbol("TERRAIN_PUSH_RIGHT"))) },
        { "push_left", tostring(read_u8(symbol("TERRAIN_PUSH_LEFT"))) },
        { "push_up", tostring(read_u8(symbol("TERRAIN_PUSH_UP"))) },
        { "push_down", tostring(read_u8(symbol("TERRAIN_PUSH_DOWN"))) },
        { "behavior", tostring(read_u8(symbol("TERRAIN_BEHAVIOR"))) },
        { "horizontal_response", tostring(signed_u16(read_u16(symbol("TERRAIN_HORIZONTAL_RESPONSE")))) },
        { "response_active", tostring(read_u8(symbol("TERRAIN_RESPONSE_ACTIVE"))) },
        { "vertical_stop", tostring(read_u8(symbol("TERRAIN_VERTICAL_STOP"))) },
        { "landing_state", tostring(read_u8(symbol("TERRAIN_LANDING_STATE"))) },
        { "surface_mode", tostring(read_u16(symbol("TERRAIN_SURFACE_MODE"))) },
        { "surface_latch", tostring(read_u8(symbol("TERRAIN_SURFACE_LATCH"))) },
        { "surface_transition_flag", tostring(read_u8(symbol("TERRAIN_SURFACE_TRANSITION_FLAG"))) },
        { "stop_left_motion", tostring(read_u8(symbol("TERRAIN_STOP_LEFT_MOTION"))) },
        { "left_inner_probe", tostring(read_u8(symbol("TERRAIN_LEFT_INNER_PROBE"))) },
        { "left_outer_probe", tostring(read_u8(symbol("TERRAIN_LEFT_OUTER_PROBE"))) },
        { "stop_right_motion", tostring(read_u8(symbol("TERRAIN_STOP_RIGHT_MOTION"))) },
        { "right_inner_probe", tostring(read_u8(symbol("TERRAIN_RIGHT_INNER_PROBE"))) },
        { "right_outer_probe", tostring(read_u8(symbol("TERRAIN_RIGHT_OUTER_PROBE"))) },
        { "stop_upward_motion", tostring(read_u8(symbol("TERRAIN_STOP_UPWARD_MOTION"))) },
        { "response_timer_state", tostring(read_u8(symbol("TERRAIN_RESPONSE_TIMER_STATE"))) },
        { "query_state_a", tostring(read_u8(symbol("TERRAIN_QUERY_STATE_A"))) },
        { "query_state_b", tostring(read_u8(symbol("TERRAIN_QUERY_STATE_B"))) },
        { "state", tostring(read_u8(symbol("TERRAIN_STATE"))) },
        { "response_latch", tostring(read_u8(symbol("TERRAIN_RESPONSE_LATCH"))) }
    })
end

local function camera_runtime_json()
    local special_mode = read_u8(symbol("CAMERA_SPECIAL_MODE"))
    return json_object({
        { "x", tostring(read_u16(symbol("WORLD_CAMERA_X"))) },
        { "y", tostring(read_u16(symbol("WORLD_CAMERA_Y"))) },
        { "reference_x", tostring(read_u16(symbol("CAMERA_REFERENCE_X"))) },
        { "reference_y", tostring(read_u16(symbol("CAMERA_REFERENCE_Y"))) },
        { "horizontal_threshold", tostring(read_u16(symbol("CAMERA_HORIZONTAL_THRESHOLD"))) },
        { "vertical_threshold", tostring(read_u16(symbol("CAMERA_VERTICAL_THRESHOLD"))) },
        { "scroll_x", tostring(signed_u16(read_u16(symbol("CAMERA_SCROLL_X")))) },
        { "scroll_y", tostring(signed_u16(read_u16(symbol("CAMERA_SCROLL_Y")))) },
        { "pixel_x", tostring(read_u16(symbol("PLAYER_CAMERA_PIXEL_X"))) },
        { "pixel_y", tostring(read_u16(symbol("PLAYER_CAMERA_PIXEL_Y"))) },
        { "tile_x", tostring(read_u16(symbol("CAMERA_TILE_X"))) },
        { "tile_y", tostring(read_u16(symbol("CAMERA_TILE_Y"))) },
        { "level_width", tostring(read_u16(symbol("LEVEL_WIDTH_PIXELS"))) },
        { "level_height", tostring(read_u16(symbol("LEVEL_HEIGHT_PIXELS"))) },
        { "update_delay", tostring(read_u8(symbol("CAMERA_UPDATE_DELAY"))) },
        { "scroll_left_pending", tostring(read_u8(symbol("CAMERA_SCROLL_LEFT_PENDING"))) },
        { "scroll_right_pending", tostring(read_u8(symbol("CAMERA_SCROLL_RIGHT_PENDING"))) },
        { "scroll_up_pending", tostring(read_u8(symbol("CAMERA_SCROLL_UP_PENDING"))) },
        { "scroll_down_pending", tostring(read_u8(symbol("CAMERA_SCROLL_DOWN_PENDING"))) },
        { "special_mode", tostring(special_mode) },
        { "state_08", json_bool(read_u8(symbol("SCENE_STATE")) == 8) }
    })
end

local function preload_machine_state()
    if preload_state == "" or preload_applied then
        return
    end
    local loaded, error_message = pcall(function ()
        if machine.debugger then
            machine.debugger:command("stateload " .. preload_state)
        else
            machine:load(preload_state)
        end
    end)
    if not loaded then
        error("could not preload MAME state " .. preload_state .. ": " .. tostring(error_message))
    end
    preload_applied = true
    scene_state_last = read_u8(scene_state_address)
    write_record({
        { "type", json_string("state_load") },
        { "frame", "0" },
        { "path", json_string(preload_state) },
        { "scene_state", tostring(scene_state_last) }
    })
    print("OpenAladdin: preloaded state " .. preload_state)
end

local register_names = { "PC", "SR" }
for index = 0, 7 do
    register_names[#register_names + 1] = "D" .. index
end
for index = 0, 7 do
    register_names[#register_names + 1] = "A" .. index
end

local function register_json()
    local result = {}
    for _, name in ipairs(register_names) do
        local value = read_register(name)
        if value ~= nil then
            result[#result + 1] = json_string(name) .. ":" .. tostring(value)
        end
    end
    return "{" .. table.concat(result, ",") .. "}"
end

local input = dofile(root .. "/re/mame/lua/input.lua")({
    core = core,
    root = root,
    write_record = write_record,
    write_state = write_state,
    write_input = capture_streams.input and capture_streams.write_input or nil,
    current_frame = function () return current_frame end
})
local experiment_action_spec = input.action_spec()

local events = dofile(root .. "/re/mame/lua/events.lua")({
    core = core,
    symbol = symbol,
    read_u8 = read_u8,
    read_u16 = read_u16,
    read_u32 = read_u32,
    signed_u16 = signed_u16,
    read_register = function () return read_register("PC") end,
    write_event = write_event,
    write_record = write_record,
    write_state = write_state,
    current_frame = function () return current_frame end,
    save_checkpoint = function (name)
        machine:save(name)
        return checkpoint_reference .. "/genesis/" .. name .. ".sta"
    end
})

local function apply_input()
    return input.apply()
end

local function input_fields_json()
    return input.fields_json()
end

local function capture(frame, input_token, emit_state)
    if emit_state == nil then
        emit_state = true
    end
    if trace_scene_states then
        local value = read_u8(scene_state_address)
        if value ~= scene_state_last then
            write_record({
                { "type", json_string("scene_state") },
                { "frame", tostring(frame) },
                { "address", tostring(scene_state_address) },
                { "previous", tostring(scene_state_last) },
                { "value", tostring(value) },
                { "pc", tostring(read_register("PC") or 0) },
                { "reason", json_string("frame_poll") }
            })
            scene_state_last = value
        end
    end
    capture_streams.dump_ram()
    dump_vdp()
    local input_port_value = input.controller_value()
    local input_mask = input.canonical_mask()
    write_record({
        { "type", json_string("frame") },
        { "frame", tostring(frame) },
        { "input", json_string(input_token or "none") },
        { "input_port_value", tostring(input_port_value) },
        { "input_mask", tostring(input_mask) },
        { "pc", tostring(read_register("PC") or 0) },
        { "sr", tostring(read_register("SR") or 0) },
        { "registers", register_json() },
        { "ram_start", tostring(ram_start) },
        { "ram_size", tostring(capture_ram and ram_size or 0) },
        { "ram_fnv1a", tostring(capture_streams.fnv1a_ram()) },
        { "scene", scene_runtime_json() },
        { "terrain", terrain_runtime_json() },
        { "vdp", vdp_state_json() }
    })
    if state and emit_state then
        local actors = {}
        for slot = 0, actor_slot_count - 1 do
            local record = actor_table_base + slot * actor_stride
            local actor_type = read_u8(record + actor_type_offset)
            local actor_flags = read_u8(record + actor_flags_offset)
            if actor_type ~= 0 or actor_flags ~= 0 then
                local actor_x = read_u16(record + actor_x_offset)
                local actor_y = read_u16(record + actor_y_offset)
                local facing_x_flip = read_u8(record + actor_facing_x_flip_offset)
                local frame_pointer = read_u32(record + actor_frame_ptr_offset)
                actors[#actors + 1] = json_object({
                    { "slot", tostring(slot) },
                    { "type", tostring(actor_type) },
                    { "x", tostring(actor_x) },
                    { "y", tostring(actor_y) },
                    { "movement_flags", tostring(read_u8(record + 0x06)) },
                    { "facing_x_flip", tostring(facing_x_flip) },
                    { "frame_ptr", tostring(frame_pointer) },
                    { "collision_box", collision_box_json(
                        frame_pointer,
                        actor_x,
                        actor_y,
                        facing_x_flip ~= 0
                    ) },
                    { "animation_pc", tostring(read_u32(record + actor_animation_pc_offset)) },
                    { "movement_pc", tostring(read_u32(record + actor_movement_pc_offset)) },
                    { "movement_loop_pc", tostring(read_u32(record + actor_movement_loop_pc_offset)) },
                    { "movement_loop_timer", tostring(read_u8(record + actor_movement_loop_timer_offset)) },
                    { "movement_word_18", tostring(signed_u16(read_u16(record + 0x18))) },
                    { "movement_word_1a", tostring(signed_u16(read_u16(record + 0x1a))) },
                    { "facing_y_flip", tostring(read_u8(record + actor_facing_y_flip_offset)) },
                    { "movement_command_timer", tostring(read_u8(record + actor_movement_command_timer_offset)) },
                    { "animation_timer", tostring(read_u8(record + actor_animation_timer_offset)) },
                    { "movement_return_pc", tostring(read_u32(record + actor_movement_return_pc_offset)) },
                    { "flags", tostring(actor_flags) },
                    { "flag_bit5", json_bool((actor_flags & 0x20) ~= 0) }
                })
            end
        end
        local player_record = actor_table_base
        local player_origin_x = read_u16(player_record + actor_x_offset)
        local player_origin_y = read_u16(player_record + actor_y_offset)
        local player_facing_x_flip = read_u8(player_record + actor_facing_x_flip_offset)
        local player_frame_pointer = read_u32(player_record + actor_frame_ptr_offset)
        write_state({
            { "type", json_string("state") },
            { "format", json_string("openaladdin-frame-state-v1") },
            { "frame", tostring(frame) },
            { "input", json_string(input_token or "none") },
            { "player", json_object({
                { "x", tostring(read_u16(symbol("PLAYER_X"))) },
                { "y", tostring(read_u16(symbol("PLAYER_Y"))) },
                { "world_x", tostring(read_u16(symbol("PLAYER_WORLD_X"))) },
                { "world_y", tostring(read_u16(symbol("PLAYER_WORLD_Y"))) },
                { "vx", tostring(signed_u16(read_u16(symbol("PLAYER_VX")))) },
                { "vy", tostring(signed_u16(read_u16(symbol("PLAYER_VY")))) },
                { "animation_pc", tostring(read_u32(symbol("PLAYER_ANIMATION_PC"))) },
                -- Player slot zero shares the common actor frame-pointer
                -- field. This is the runtime sprite identity used by the
                -- native animation differential test.
                { "frame_ptr", tostring(player_frame_pointer) },
                -- The player is actor-table slot zero; expose its raw
                -- horizontal flip byte alongside the frame pointer so
                -- native parity covers the rendered facing state too.
                { "facing_x_flip", tostring(player_facing_x_flip) },
                { "collision_box", collision_box_json(
                    player_frame_pointer,
                    player_origin_x,
                    player_origin_y,
                    player_facing_x_flip ~= 0
                ) },
                { "animation_timer", tostring(read_u8(symbol("PLAYER_ANIMATION_TIMER"))) },
                { "animation_selector", json_object({
                    { "animation_gate", tostring(read_u8(0xFFF0E7)) },
                    { "terminal_transition", tostring(read_u8(0xFFF0E6)) },
                    { "scene_script_countdown", tostring(read_u8(0xFFF0E9)) },
                    { "interaction_lock", tostring(read_u8(0xFFF0F2)) },
                    { "response_active", tostring(read_u8(0xFFF0BE)) },
                    { "landing_state", tostring(read_u8(0xFFF0C1)) },
                    { "transition_gate", tostring(read_u8(0xFFF0D0)) },
                    { "transition_lock", tostring(read_u8(0xFFF0D7)) },
                    { "transition_state", tostring(read_u8(0xFFF0DB)) },
                    { "transition_mode", tostring(read_u8(0xFFF0CD)) },
                    { "transition_flag", tostring(read_u8(0xFFF0D2)) },
                    { "transition_response", tostring(read_u8(0xFFF0D4)) },
                    { "transition_state_de", tostring(read_u8(0xFFF0DE)) },
                    { "transition_state_df", tostring(read_u8(0xFFF0DF)) },
                    { "camera_special_mode", tostring(read_u8(0xFFF173)) },
                    { "response_latch", tostring(read_u8(0xFFF115)) },
                    { "response_animation", tostring(read_u8(0xFFF0ED)) },
                    { "response_state_ee", tostring(read_u8(0xFFF0EE)) },
                    { "response_state_ef", tostring(read_u8(0xFFF0EF)) },
                    { "response_state_f0", tostring(read_u8(0xFFF0F0)) },
                    { "response_state_101", tostring(read_u8(0xFFF101)) },
                    { "horizontal_response", tostring(signed_u16(read_u16(0xFFF0B0))) },
                    { "response_timer", tostring(read_u8(0xFFF0CC)) },
                    { "interaction_pending", tostring(read_u8(0xFFEFFF)) },
                    { "state_lock", tostring(read_u8(0xFFF11F)) }
                }) },
                { "actor_flags", tostring(read_u8(symbol("PLAYER_ACTOR_FLAGS"))) },
                { "actor_flag_bit5", json_bool((read_u8(symbol("PLAYER_ACTOR_FLAGS")) & 0x20) ~= 0) },
                -- Player_Update arms FFEFFF with the ten-frame action delay
                -- when the sword selector chooses PLAYER_ANIM_SWORD.
                { "attack_timer", tostring(read_u8(symbol("PLAYER_INTERACTION_PENDING"))) },
                { "attack_active", json_bool(read_u8(symbol("PLAYER_INTERACTION_PENDING")) ~= 0) },
                -- TERRAIN_LANDING_STATE is the ROM's explicit grounded/landing
                -- state. PLAYER_VY can be zero during an airborne vertical
                -- stop, so it is not a safe grounded predicate.
                { "grounded", json_bool(read_u8(symbol("TERRAIN_LANDING_STATE")) == 1) }
            }) },
            { "scene", json_object({
                { "state", tostring(read_u8(symbol("SCENE_STATE"))) },
                { "script_cursor", tostring(read_u32(symbol("SCENE_SCRIPT_CURSOR"))) },
                { "script_data_cursor", tostring(read_u32(symbol("SCENE_SCRIPT_DATA"))) },
                { "table_index", tostring(read_u16(symbol("SCENE_TABLE_INDEX"))) },
                { "script_pending", tostring(read_u8(symbol("SCENE_SCRIPT_PENDING"))) },
                { "vdp_update", tostring(read_u8(symbol("SCENE_VDP_UPDATE_FLAG"))) },
                { "vdp_clear", tostring(read_u8(symbol("SCENE_VDP_CLEAR_FLAG"))) },
                { "transition_event", tostring(read_u8(symbol("SCENE_TRANSITION_EVENT"))) },
                { "script_countdown", tostring(read_u8(symbol("SCENE_SCRIPT_COUNTDOWN"))) },
                { "script_gate", tostring(read_u8(symbol("SCENE_SCRIPT_GATE"))) },
                { "player_gate", tostring(read_u8(symbol("PLAYER_TRANSITION_GATE"))) },
                { "player_lock", tostring(read_u8(symbol("PLAYER_TRANSITION_LOCK"))) },
                { "player_countdown", tostring(read_u8(symbol("PLAYER_TRANSITION_COUNTDOWN"))) },
                { "player_terminal", tostring(read_u8(symbol("PLAYER_TERMINAL_TRANSITION"))) }
            }) },
            { "camera", camera_runtime_json() },
            { "terrain", terrain_runtime_json() },
            { "actors", json_array(actors) }
        })
    end
end

-- frame_done runs at the video boundary, which can interrupt the 68000 in
-- the middle of Player_Update/Camera_UpdateFollow. In synchronized mode the
-- debugger records the semantic RAM state at the game's update-loop boundary;
-- tools/oa.py folds those records back into state.jsonl after MAME exits.
if state_sync then
    if not cpu.debug then
        error("OPENALADDIN_STATE_SYNC requires MAME debugger support")
    end
    local function sync_memory(width, name)
        return string.format(":maincpu.%s@$%06X", width, symbol(name))
    end
    local sync_action = string.format(
        "printf \"OPENALADDIN_SYNC frame=%%d pc=%%08X x=%%04X y=%%04X wx=%%04X wy=%%04X vx=%%04X vy=%%04X grounded=%%02X frameptr=%%08X facing=%%02X animpc=%%08X animtimer=%%02X camx=%%04X camy=%%04X refx=%%04X refy=%%04X sx=%%04X sy=%%04X thx=%%04X thy=%%04X delay=%%02X special=%%02X\\n\",frame,pc,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s ; g",
        sync_memory("w", "PLAYER_X"),
        sync_memory("w", "PLAYER_Y"),
        sync_memory("w", "PLAYER_WORLD_X"),
        sync_memory("w", "PLAYER_WORLD_Y"),
        sync_memory("w", "PLAYER_VX"),
        sync_memory("w", "PLAYER_VY"),
        sync_memory("b", "TERRAIN_LANDING_STATE"),
        sync_memory("d", "PLAYER_FRAME_PTR"),
        sync_memory("b", "PLAYER_FACING_X_FLIP"),
        sync_memory("d", "PLAYER_ANIMATION_PC"),
        sync_memory("b", "PLAYER_ANIMATION_TIMER"),
        sync_memory("w", "WORLD_CAMERA_X"),
        sync_memory("w", "WORLD_CAMERA_Y"),
        sync_memory("w", "CAMERA_REFERENCE_X"),
        sync_memory("w", "CAMERA_REFERENCE_Y"),
        sync_memory("w", "CAMERA_SCROLL_X"),
        sync_memory("w", "CAMERA_SCROLL_Y"),
        sync_memory("w", "CAMERA_HORIZONTAL_THRESHOLD"),
        sync_memory("w", "CAMERA_VERTICAL_THRESHOLD"),
        sync_memory("b", "CAMERA_UPDATE_DELAY"),
        sync_memory("b", "CAMERA_SPECIAL_MODE")
    )
    -- Gameplay and title/scene modes use different outer loops. VBlankInterrupt
    -- is hit once per emulated frame after the gameplay work
    -- has completed. Allow a targeted run to place the semantic checkpoint at
    -- another boundary when a narrower experiment needs it.
    local sync_pc = math.floor(env_number("OPENALADDIN_SYNC_PC", symbol("VBlankInterrupt")))
    cpu.debug:bpset(
        sync_pc,
        "",
        sync_action)
end

local function capture_artifacts(frame)
    local checkpoint_name = checkpoints[frame]
    if checkpoint_name then
        machine:save(checkpoint_name)
        write_record({
            { "type", json_string("checkpoint") },
            { "frame", tostring(frame) },
            { "name", json_string(checkpoint_name) },
            { "state", json_string(checkpoint_reference .. "/genesis/" .. checkpoint_name .. ".sta") }
        })
        if state then
            write_state({
                { "type", json_string("checkpoint") },
                { "format", json_string("openaladdin-frame-state-v1") },
                { "frame", tostring(frame) },
                { "name", json_string(checkpoint_name) },
                { "state", json_string(checkpoint_reference .. "/genesis/" .. checkpoint_name .. ".sta") }
            })
        end
        print(string.format(
            "OpenAladdin: checkpoint %q saved at frame %d",
            checkpoint_name,
            frame))
    end

    if frame == save_frame then
        machine:save(save_name)
        print(string.format("OpenAladdin: scheduled save state %q at frame %d", save_name, frame))
    end

    if frame == snapshot_frame then
        local captured = 0
        for _, screen in pairs(machine.screens) do
            local result = screen:snapshot(snapshot_name)
            if result == nil then
                captured = captured + 1
            else
                print(string.format("OpenAladdin: snapshot failed: %s", tostring(result)))
            end
        end
        print(string.format("OpenAladdin: captured %d screen snapshot(s) at frame %d", captured, frame))
    end
end

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

local memory_pokes = {}
local memory_poke_spec = os.getenv("OPENALADDIN_POKE_MEMORY") or ""
for item in memory_poke_spec:gmatch("[^,]+") do
    local address_text, rhs = item:match("^%s*([^=]+)%s*=%s*(.-)%s*$")
    if address_text and rhs then
        local value_text, width = rhs:match("^%s*([^:]+)%s*:%s*([uU]%d+)%s*$")
        if width then
            width = width:lower()
        end
        if not value_text then
            value_text = rhs
            width = "u8"
        end
        local address = parse_hex_address(address_text)
        local value = parse_integer(value_text)
        if address and value and (width == "u8" or width == "u16" or width == "u32") then
            memory_pokes[#memory_pokes + 1] = {
                address = address,
                value = value,
                width = width
            }
        end
    end
end

local function apply_memory_pokes(frame)
    if frame ~= poke_frame then
        return
    end
    for _, poke in ipairs(memory_pokes) do
        if poke.width == "u16" then
            space:write_u16(poke.address, poke.value & 0xffff)
        elseif poke.width == "u32" then
            space:write_u32(poke.address, poke.value & 0xffffffff)
        else
            space:write_u8(poke.address, poke.value & 0xff)
        end
        write_record({
            { "type", json_string("memory_poke") },
            { "frame", tostring(frame) },
            { "address", tostring(poke.address) },
            { "value", tostring(poke.value) },
            { "width", json_string(poke.width) }
        })
    end
    if #memory_pokes > 0 then
        print(string.format(
            "OpenAladdin: applied %d memory poke(s) at frame %d",
            #memory_pokes,
            frame))
    end
end

local breakpoint_list = os.getenv("OPENALADDIN_BREAKPOINTS") or ""
local trace_actor_initializers = os.getenv("OPENALADDIN_TRACE_ACTOR_INIT") == "1"
local trace_rnc_loads = os.getenv("OPENALADDIN_TRACE_RNC_LOADS") == "1"
local inject_actor_frame = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FRAME", -1))
local inject_actor_slot = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_SLOT", 31))
local inject_actor_type = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_TYPE", 0x7d))
local inject_actor_pc = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_PC", symbol("ACTOR_ANIM_STATE_125952")))
local inject_actor_template = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_TEMPLATE", symbol("ACTOR_TEMPLATE_TYPE_7D")))
local inject_actor_x = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_X", -1))
local inject_actor_y = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_Y", -1))
local inject_actor_frame_ptr = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FRAME_PTR", -1))
local inject_actor_word_18 = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_WORD_18", -0x10000))
local inject_actor_word_1a = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_WORD_1A", -0x10000))
local inject_actor_movement_pc = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC", -1))
local inject_actor_loop_pc = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_MOVEMENT_LOOP_PC", -1))
local inject_actor_loop_timer = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_MOVEMENT_LOOP_TIMER", -1))
local inject_actor_facing_x = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FACING_X", -1))
local inject_actor_facing_y = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FACING_Y", -1))
local inject_actor_flags = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FLAGS", -1))
local inject_actor_runtime_flags = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_RUNTIME_FLAGS", -1))
local inject_actor_movement_timer = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_MOVEMENT_TIMER", -1))
local inject_actor_return_pc = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_RETURN_PC", -1))

local watches = dofile(root .. "/re/mame/lua/watches.lua")({
    core = core,
    space = space,
    cpu = cpu,
    symbol = symbol,
    write_record = write_record,
    read_u8 = read_u8,
    read_u32 = read_u32,
    read_register = read_register,
    current_frame = function () return current_frame end,
    trace_scene_states = trace_scene_states,
    trace_selector = trace_selector,
    trace_actors = trace_actors,
    actor_table_base = actor_table_base,
    actor_stride = actor_stride,
    actor_slot_count = actor_slot_count,
    actor_type_offset = actor_type_offset,
    actor_animation_pc_offset = actor_animation_pc_offset,
    actor_flags_offset = actor_flags_offset,
    scene_state_address = scene_state_address,
    get_scene_state_last = function () return scene_state_last end,
    set_scene_state_last = function (value) scene_state_last = value end,
    trace_actor_initializers = trace_actor_initializers,
    trace_rnc_loads = trace_rnc_loads,
    trace_audio_commands = trace_audio_commands,
    trace_audio_mailbox = trace_audio_mailbox,
    trace_audio_mailbox_reads = trace_audio_mailbox_reads
})
local watched_addresses = watches.watched_addresses

local actors = dofile(root .. "/re/mame/lua/actors.lua")({
    space = space,
    symbol = symbol,
    read_u16 = read_u16,
    actor_table_base = actor_table_base,
    actor_stride = actor_stride,
    actor_slot_count = actor_slot_count,
    actor_type_offset = actor_type_offset,
    actor_x_offset = actor_x_offset,
    actor_y_offset = actor_y_offset,
    actor_movement_pc_offset = actor_movement_pc_offset,
    actor_frame_ptr_offset = actor_frame_ptr_offset,
    actor_animation_pc_offset = actor_animation_pc_offset,
    injection_frame = inject_actor_frame,
    injection_slot = inject_actor_slot,
    injection_type = inject_actor_type,
    injection_pc = inject_actor_pc,
    injection_template = inject_actor_template,
    injection_x = inject_actor_x,
    injection_y = inject_actor_y,
    injection_frame_ptr = inject_actor_frame_ptr,
    injection_word_18 = inject_actor_word_18,
    injection_word_1a = inject_actor_word_1a,
    injection_movement_pc = inject_actor_movement_pc,
    injection_loop_pc = inject_actor_loop_pc,
    injection_loop_timer = inject_actor_loop_timer,
    injection_facing_x = inject_actor_facing_x,
    injection_facing_y = inject_actor_facing_y,
    injection_flags = inject_actor_flags,
    injection_runtime_flags = inject_actor_runtime_flags,
    injection_movement_timer = inject_actor_movement_timer,
    injection_return_pc = inject_actor_return_pc
})

local function port_tags_json()
    local tags = {}
    for tag in pairs(machine.ioport.ports) do
        tags[#tags + 1] = json_string(tag)
    end
    table.sort(tags)
    return json_array(tags)
end

local shutdown_complete = false
local function shutdown()
    if shutdown_complete then
        return
    end
    shutdown_complete = true
    if audio.close then audio.close() end
    capture_streams.close()
end

write_record({
    { "type", json_string("header") },
    { "system", json_string(emu.romname()) },
    { "game", json_string(emu.gamename()) },
    { "cpu_tag", json_string(cpu.tag) },
    { "cpu_name", json_string(cpu.name) },
    { "program_data_width", tostring(space.data_width) },
    { "program_address_shift", tostring(space.shift) },
    { "program_address_mask", tostring(space.address_mask) },
    { "frame_limit", tostring(frame_limit) },
    { "capture_profile", json_string(capture_profile) },
    { "audio_trace", json_bool(trace_audio) },
    { "audio_mailbox_trace", json_bool(trace_audio_mailbox) },
    { "audio_mailbox_read_trace", json_bool(trace_audio_mailbox_reads) },
    { "audio_command_trace", json_bool(trace_audio_commands) },
    { "ram_start", tostring(ram_start) },
    { "ram_size", tostring(capture_ram and ram_size or 0) },
    { "reset_ssp", tostring(read_u32(0)) },
    { "reset_pc", tostring(read_u32(4)) },
    { "vdp_device", vdp_device and json_string(vdp_device.tag) or "null" },
    { "vdp_vram_bytes", tostring(vdp.header_sizes().vram) },
    { "vdp_cram_bytes", tostring(vdp.header_sizes().cram) },
    { "vdp_vsram_bytes", tostring(vdp.header_sizes().vsram) },
    { "vdp_regs_bytes", tostring(vdp.header_sizes().regs) },
    { "vdp_items", vdp_items_json() },
    { "input_ports", port_tags_json() },
    { "player1_input_fields", input_fields_json() },
    { "watched_addresses", json_array((function ()
        local values = {}
        for index, address in ipairs(watched_addresses) do
            values[index] = tostring(address)
        end
        return values
    end)()) },
    { "actor_trace", json_bool(trace_actors) },
    { "actor_table_base", tostring(actor_table_base) },
    { "actor_stride", tostring(actor_stride) },
    { "actor_slot_count", tostring(actor_slot_count) },
    { "actor_type_offset", tostring(actor_type_offset) },
    { "actor_active_offset", tostring(actor_type_offset) },
    { "actor_x_offset", tostring(actor_x_offset) },
    { "actor_y_offset", tostring(actor_y_offset) },
    { "actor_movement_pc_offset", tostring(actor_movement_pc_offset) },
    { "actor_movement_loop_pc_offset", tostring(actor_movement_loop_pc_offset) },
    { "actor_movement_loop_timer_offset", tostring(actor_movement_loop_timer_offset) },
    { "actor_frame_ptr_offset", tostring(actor_frame_ptr_offset) },
    { "actor_animation_pc_offset", tostring(actor_animation_pc_offset) },
    { "actor_facing_x_flip_offset", tostring(actor_facing_x_flip_offset) },
    { "actor_facing_y_flip_offset", tostring(actor_facing_y_flip_offset) },
    { "actor_movement_command_timer_offset", tostring(actor_movement_command_timer_offset) },
    { "actor_movement_return_pc_offset", tostring(actor_movement_return_pc_offset) },
    { "actor_flags_offset", tostring(actor_flags_offset) },
    { "actor_initializer_trace", json_bool(trace_actor_initializers) },
    { "rnc_loader_trace", json_bool(trace_rnc_loads) },
    { "scene_state_trace", json_bool(trace_scene_states) },
    { "state_trace", json_bool(state_output) },
    { "experiment_actions", json_string(experiment_action_spec) },
    { "event_detectors", json_string(event_spec) },
    { "scene_state_address", tostring(scene_state_address) },
    { "memory_poke_frame", tostring(poke_frame) },
    { "memory_poke_spec", json_string(memory_poke_spec) },
    { "checkpoint_spec", json_string(checkpoint_spec) },
    { "preload_state", json_string(preload_state) },
    { "breakpoint_list", json_string(breakpoint_list) },
    { "edge_trace", json_bool(edge_tracer.enabled) },
    { "edge_target_count", tostring(edge_tracer.target_count) },
    { "edge_table_count", tostring(edge_tracer.table_count) },
    { "actor_injection_frame", tostring(inject_actor_frame) },
    { "actor_injection_slot", tostring(inject_actor_slot) },
    { "actor_injection_type", tostring(inject_actor_type) },
    { "actor_injection_pc", tostring(inject_actor_pc) },
    { "actor_injection_template", tostring(inject_actor_template) }
})

if state then
    state:write(json_object({
        { "type", json_string("header") },
        { "format", json_string("openaladdin-frame-state-v1") },
        { "rom", json_string(emu.romname()) },
        { "rom_sha256", json_string(os.getenv("OPENALADDIN_ROM_SHA256") or "") },
        { "frame_limit", tostring(frame_limit) },
        { "event_detectors", json_string(event_spec) },
        { "player_ram", json_object({
                { "x", tostring(symbol("PLAYER_X")) },
                { "y", tostring(symbol("PLAYER_Y")) },
                { "vx", tostring(symbol("PLAYER_VX")) },
                { "vy", tostring(symbol("PLAYER_VY")) },
                { "animation_pc", tostring(symbol("PLAYER_ANIMATION_PC")) }
        }) },
        { "terrain_ram", json_object({
                { "query_result", tostring(symbol("TERRAIN_QUERY_FLAGS")) },
                { "push_right", tostring(symbol("TERRAIN_PUSH_RIGHT")) },
                { "push_left", tostring(symbol("TERRAIN_PUSH_LEFT")) },
                { "push_up", tostring(symbol("TERRAIN_PUSH_UP")) },
                { "push_down", tostring(symbol("TERRAIN_PUSH_DOWN")) },
                { "behavior", tostring(symbol("TERRAIN_BEHAVIOR")) },
                { "horizontal_response", tostring(symbol("TERRAIN_HORIZONTAL_RESPONSE")) },
                { "response_active", tostring(symbol("TERRAIN_RESPONSE_ACTIVE")) },
                { "vertical_stop", tostring(symbol("TERRAIN_VERTICAL_STOP")) },
                { "landing_state", tostring(symbol("TERRAIN_LANDING_STATE")) },
                { "surface_mode", tostring(symbol("TERRAIN_SURFACE_MODE")) },
                { "surface_latch", tostring(symbol("TERRAIN_SURFACE_LATCH")) },
                { "surface_transition_flag", tostring(symbol("TERRAIN_SURFACE_TRANSITION_FLAG")) },
                { "stop_left_motion", tostring(symbol("TERRAIN_STOP_LEFT_MOTION")) },
                { "left_inner_probe", tostring(symbol("TERRAIN_LEFT_INNER_PROBE")) },
                { "left_outer_probe", tostring(symbol("TERRAIN_LEFT_OUTER_PROBE")) },
                { "stop_right_motion", tostring(symbol("TERRAIN_STOP_RIGHT_MOTION")) },
                { "right_inner_probe", tostring(symbol("TERRAIN_RIGHT_INNER_PROBE")) },
                { "right_outer_probe", tostring(symbol("TERRAIN_RIGHT_OUTER_PROBE")) },
                { "stop_upward_motion", tostring(symbol("TERRAIN_STOP_UPWARD_MOTION")) },
                { "response_timer_state", tostring(symbol("TERRAIN_RESPONSE_TIMER_STATE")) },
                { "query_state_a", tostring(symbol("TERRAIN_QUERY_STATE_A")) },
                { "query_state_b", tostring(symbol("TERRAIN_QUERY_STATE_B")) },
                { "state", tostring(symbol("TERRAIN_STATE")) },
                { "response_latch", tostring(symbol("TERRAIN_RESPONSE_LATCH")) }
        }) },
        { "actor_table_base", tostring(actor_table_base) },
        { "actor_stride", tostring(actor_stride) }
    }), "\n")
    state:flush()
end

if trace_scene_states then
    write_record({
        { "type", json_string("scene_state") },
        { "frame", "0" },
        { "address", tostring(scene_state_address) },
        { "value", tostring(scene_state_last) },
        { "pc", tostring(read_register("PC") or 0) },
        { "reason", json_string("initial") }
    })
end

actors.inject(0)
if preload_state == "" then
    apply_memory_pokes(0)
end
capture(0, apply_input(0), true)
events.poll(0)
if audio.dump_driver then audio.dump_driver(0, "initial") end
capture_artifacts(0)

emu.register_frame_done(function ()
    current_frame = current_frame + 1
    if frame_limit >= 0 and current_frame > frame_limit then
        shutdown()
        machine:exit()
        return
    end

    if current_frame == 1 then
        preload_machine_state()
        if preload_state ~= "" then
            apply_memory_pokes(0)
        end
    end
    actors.inject(current_frame)
    apply_memory_pokes(current_frame)
    -- Synchronized player fields are merged from the debugger boundary, but
    -- actor records are not part of the compact OPENALADDIN_SYNC line. Keep a
    -- frame-state sample when actor tracing is requested so the merge can
    -- preserve the live slot table for differential combat probes.
    capture(current_frame, apply_input(current_frame), not state_sync or trace_actors)
    events.poll(current_frame)
    if audio.dump_driver then audio.dump_driver(current_frame, "frame") end
    capture_artifacts(current_frame)

    if frame_limit >= 0 and current_frame == frame_limit then
        shutdown()
        machine:exit()
    end
end)
