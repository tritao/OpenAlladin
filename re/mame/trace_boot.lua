-- OpenAladdin first-pass MAME trace harness.
--
-- This script intentionally does not assume any discovered game symbols yet.
-- It records the 68000 reset vectors, CPU registers, the mapped Genesis RAM
-- window, and the visible input fields so later experiments have a stable,
-- machine-readable starting point.
--
-- Run through tools/mame-trace.sh rather than invoking this file directly.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]

if not cpu then
    error("MAME did not expose :maincpu")
end

local space = cpu.spaces["program"]
if not space then
    error("MAME main CPU has no program address space")
end

local function env_number(name, fallback)
    local value = tonumber(os.getenv(name) or "")
    if value == nil then
        return fallback
    end
    return value
end

local trace_dir = os.getenv("OPENALADDIN_TRACE_DIR") or "build/re/traces"
local frame_limit = math.max(0, math.floor(env_number("OPENALADDIN_TRACE_FRAMES", 120)))
local save_frame = math.floor(env_number("OPENALADDIN_SAVE_FRAME", -1))
local snapshot_frame = math.floor(env_number("OPENALADDIN_SNAPSHOT_FRAME", -1))
local save_name = os.getenv("OPENALADDIN_SAVE_NAME") or "gameplay"
local snapshot_name = os.getenv("OPENALADDIN_SNAPSHOT_NAME") or "gameplay.png"
local ram_start = 0xff0000
local ram_size = 0x10000

local function join_path(dir, name)
    if dir:sub(-1) == "/" then
        return dir .. name
    end
    return dir .. "/" .. name
end

local trace_path = join_path(trace_dir, "trace_boot.jsonl")
local ram_path = join_path(trace_dir, "ram_frames.bin")
local vdp_vram_path = join_path(trace_dir, "vdp_vram_frames.bin")
local vdp_cram_path = join_path(trace_dir, "vdp_cram_frames.bin")
local vdp_vsram_path = join_path(trace_dir, "vdp_vsram_frames.bin")
local vdp_regs_path = join_path(trace_dir, "vdp_regs_frames.bin")
local vdp_writes_path = join_path(trace_dir, "vdp_writes.jsonl")

local trace = assert(io.open(trace_path, "wb"))
local ram = assert(io.open(ram_path, "wb"))
local vdp_vram = assert(io.open(vdp_vram_path, "wb"))
local vdp_cram = assert(io.open(vdp_cram_path, "wb"))
local vdp_vsram = assert(io.open(vdp_vsram_path, "wb"))
local vdp_regs = assert(io.open(vdp_regs_path, "wb"))
local vdp_writes = assert(io.open(vdp_writes_path, "wb"))

local function json_escape(value)
    return value:gsub("[\\\"\n\r\t]", function(char)
        if char == "\\" then return "\\\\" end
        if char == "\"" then return "\\\"" end
        if char == "\n" then return "\\n" end
        if char == "\r" then return "\\r" end
        return "\\t"
    end)
end

local function json_string(value)
    return "\"" .. json_escape(value) .. "\""
end

local function json_bool(value)
    return value and "true" or "false"
end

local function json_array(values)
    local result = {}
    for index, value in ipairs(values) do
        result[index] = value
    end
    return "[" .. table.concat(result, ",") .. "]"
end

local function json_object(fields)
    local result = {}
    for _, field in ipairs(fields) do
        result[#result + 1] = json_string(field[1]) .. ":" .. field[2]
    end
    return "{" .. table.concat(result, ",") .. "}"
end

local function write_record(fields)
    trace:write(json_object(fields), "\n")
    trace:flush()
end

local function read_u8(address)
    return space:read_u8(address) & 0xff
end

local function read_u16(address)
    return space:read_u16(address) & 0xffff
end

local function read_u32(address)
    return space:read_u32(address) & 0xffffffff
end

local function find_device(tag, shortname)
    local device = machine.devices[tag]
    if device then
        return device
    end
    for candidate_tag, candidate in pairs(machine.devices) do
        if (shortname and candidate.shortname == shortname)
            or (candidate_tag:find("gen_vdp", 1, true) ~= nil) then
            return candidate
        end
    end
    return nil
end

local vdp_device = find_device(":gen_vdp", "sega315_5313")

local function find_save_item(device, name)
    if not device then
        return nil
    end
    local index = device.items[name] or device.items["0/" .. name]
    if index == nil then
        for item_name, item_index in pairs(device.items) do
            if item_name:match("/" .. name .. "$") then
                index = item_index
                break
            end
        end
    end
    if index == nil then
        return nil
    end
    return emu.item(index)
end

local vdp_vram_item = find_save_item(vdp_device, "m_vram")
local vdp_cram_item = find_save_item(vdp_device, "m_cram")
local vdp_vsram_item = find_save_item(vdp_device, "m_vsram")
local vdp_regs_item = find_save_item(vdp_device, "m_regs")
local vdp_address_item = find_save_item(vdp_device, "m_vdp_address")
local vdp_code_item = find_save_item(vdp_device, "m_vdp_code")
local vdp_command_pending_item = find_save_item(vdp_device, "m_command_pending")
local capture_vdp = os.getenv("OPENALADDIN_CAPTURE_VDP") ~= "0"

local function item_word(item, index)
    if not item then
        return 0
    end
    return (item:read(index) or 0) & 0xffff
end

local function dump_item_words(item, count, output)
    local chunks = {}
    for index = 0, count - 1 do
        chunks[#chunks + 1] = string.pack(">I2", item_word(item, index))
    end
    output:write(table.concat(chunks))
end

local function fnv1a_item(item, count)
    local hash = 2166136261
    for index = 0, count - 1 do
        local value = item_word(item, index)
        hash = (hash ~ ((value >> 8) & 0xff)) & 0xffffffff
        hash = (hash * 16777619) & 0xffffffff
        hash = (hash ~ (value & 0xff)) & 0xffffffff
        hash = (hash * 16777619) & 0xffffffff
    end
    return hash
end

local function vdp_registers_json()
    if not vdp_regs_item then
        return "[]"
    end
    local values = {}
    for index = 0, 31 do
        values[#values + 1] = tostring(item_word(vdp_regs_item, index) & 0xff)
    end
    return "[" .. table.concat(values, ",") .. "]"
end

local function vdp_state_json()
    if not capture_vdp or not vdp_device then
        return "null"
    end
    return json_object({
        { "device", json_string(vdp_device.tag) },
        { "address", tostring(item_word(vdp_address_item, 0)) },
        { "code", tostring(item_word(vdp_code_item, 0)) },
        { "command_pending", tostring(item_word(vdp_command_pending_item, 0)) },
        { "registers", vdp_registers_json() },
        { "vram_fnv1a", tostring(fnv1a_item(vdp_vram_item, 0x10000 / 2)) },
        { "cram_fnv1a", tostring(fnv1a_item(vdp_cram_item, 0x80 / 2)) },
        { "vsram_fnv1a", tostring(fnv1a_item(vdp_vsram_item, 0x80 / 2)) }
    })
end

local function vdp_items_json()
    if not vdp_device then
        return "[]"
    end
    local names = {}
    for name in pairs(vdp_device.items) do
        names[#names + 1] = json_string(name)
    end
    table.sort(names)
    return json_array(names)
end

local function dump_vdp()
    if not capture_vdp or not (vdp_vram_item and vdp_cram_item and vdp_vsram_item and vdp_regs_item) then
        return
    end
    dump_item_words(vdp_vram_item, 0x10000 / 2, vdp_vram)
    dump_item_words(vdp_cram_item, 0x80 / 2, vdp_cram)
    dump_item_words(vdp_vsram_item, 0x80 / 2, vdp_vsram)
    dump_item_words(vdp_regs_item, 0x40 / 2, vdp_regs)
    vdp_vram:flush()
    vdp_cram:flush()
    vdp_vsram:flush()
    vdp_regs:flush()
end

local function read_register(name)
    local entry = cpu.state[name]
    if not entry then
        return nil
    end
    return entry.value
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

local function fnv1a_ram()
    local hash = 2166136261
    for offset = 0, ram_size - 1 do
        hash = hash ~ read_u8(ram_start + offset)
        hash = (hash * 16777619) & 0xffffffff
    end
    return hash
end

local function dump_ram()
    local chunks = {}
    local chunk = {}
    local chunk_size = 4096

    for offset = 0, ram_size - 1 do
        chunk[#chunk + 1] = string.char(read_u8(ram_start + offset))
        if #chunk == chunk_size then
            chunks[#chunks + 1] = table.concat(chunk)
            chunk = {}
        end
    end

    if #chunk > 0 then
        chunks[#chunks + 1] = table.concat(chunk)
    end

    ram:write(table.concat(chunks))
    ram:flush()
end

local input_fields = {}
local input_field_names = {}
local controller_port = machine.ioport.ports[":ctrl1:mdpad:PAD"]

for port_tag, port in pairs(machine.ioport.ports) do
    for field_name, field in pairs(port.fields) do
        local normalized = field_name:lower()
        if normalized:find("p1", 1, true)
            or normalized:find("player 1", 1, true)
            or normalized:find("1p", 1, true) then
            input_fields[#input_fields + 1] = {
                name = field_name,
                normalized = normalized,
                field = field,
                port = port_tag
            }
            input_field_names[#input_field_names + 1] = json_string(field_name)
        end
    end
end

table.sort(input_fields, function(left, right)
    return left.name < right.name
end)
table.sort(input_field_names)

local function find_input_field(words)
    for _, entry in ipairs(input_fields) do
        local matches = true
        for _, word in ipairs(words) do
            if not entry.normalized:find(word, 1, true) then
                matches = false
                break
            end
        end
        if matches then
            return entry.field
        end
    end
    return nil
end

local function find_input_field_any(candidates)
    for _, words in ipairs(candidates) do
        local field = find_input_field(words)
        if field then
            return field
        end
    end
    return nil
end

local controls = {
    up = find_input_field({ "up" }),
    down = find_input_field({ "down" }),
    left = find_input_field({ "left" }),
    right = find_input_field({ "right" }),
    a = find_input_field_any({ { "button 1" }, { "p1 a" }, { "player 1 a" } }),
    b = find_input_field_any({ { "button 2" }, { "p1 b" }, { "player 1 b" } }),
    c = find_input_field_any({ { "button 3" }, { "p1 c" }, { "player 1 c" } }),
    start = find_input_field({ "start" })
}

local function split(value, separator)
    local result = {}
    for item in (value .. separator):gmatch("(.-)" .. separator) do
        result[#result + 1] = item
    end
    return result
end

local input_schedule = split(os.getenv("OPENALADDIN_INPUT") or "none", ",")
local input_tokens = {}

for _, item in ipairs(input_schedule) do
    local token, count = item:match("^%s*(.-)%s*[*:](%d+)%s*$")
    if token then
        for _ = 1, tonumber(count) do
            input_tokens[#input_tokens + 1] = token:lower()
        end
    else
        input_tokens[#input_tokens + 1] = item:lower():gsub("^%s+", ""):gsub("%s+$", "")
    end
end

local function clear_inputs()
    for _, entry in ipairs(input_fields) do
        entry.field:set_value(0)
    end
end

local function apply_input(frame)
    clear_inputs()
    -- A finite schedule releases all controls after its final token.
    local token = input_tokens[frame + 1] or "none"
    if token == "" or token == "none" then
        return token
    end

    for part in token:gmatch("[^+]+") do
        local field = controls[part]
        if field then
            field:set_value(1)
        end
    end
    return token
end

local function input_fields_json()
    return json_array(input_field_names)
end

local function capture(frame, input_token)
    dump_ram()
    dump_vdp()
    local input_port_value = controller_port and controller_port:read() or 0
    write_record({
        { "type", json_string("frame") },
        { "frame", tostring(frame) },
        { "input", json_string(input_token or "none") },
        { "input_port_value", tostring(input_port_value) },
        { "pc", tostring(read_register("PC") or 0) },
        { "sr", tostring(read_register("SR") or 0) },
        { "registers", register_json() },
        { "ram_start", tostring(ram_start) },
        { "ram_size", tostring(ram_size) },
        { "ram_fnv1a", tostring(fnv1a_ram()) },
        { "vdp", vdp_state_json() }
    })
end

local function capture_artifacts(frame)
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

local watch_taps = {}
local vdp_taps = {}
local watched_addresses = {}
local current_frame = 0
local watch_list = os.getenv("OPENALADDIN_WATCH_ADDRESSES") or ""
local debugger_watch = os.getenv("OPENALADDIN_DEBUG_WATCH") == "1"
local trace_actor_initializers = os.getenv("OPENALADDIN_TRACE_ACTOR_INIT") == "1"
local trace_actors = os.getenv("OPENALADDIN_TRACE_ACTORS") == "1"
local actor_table_base = 0xff7e40
local actor_stride = 0x42
local actor_slot_count = math.max(0, math.floor(env_number("OPENALADDIN_ACTOR_SLOTS", 32)))
local actor_type_offset = 0x00
local actor_movement_pc_offset = 0x0a
local actor_animation_pc_offset = 0x20
local inject_actor_frame = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_FRAME", -1))
local inject_actor_slot = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_SLOT", 31))
local inject_actor_type = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_TYPE", 0x7d))
local inject_actor_pc = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_PC", 0x125952))
local inject_actor_template = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_TEMPLATE", 0x1b81d8))
local inject_actor_x = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_X", -1))
local inject_actor_y = math.floor(env_number("OPENALADDIN_INJECT_ACTOR_Y", -1))

for item in watch_list:gmatch("[^,]+") do
    local address = parse_hex_address(item)
    if address then
        watched_addresses[#watched_addresses + 1] = address
        local name = string.format("openaladdin_watch_%06X", address)
        watch_taps[#watch_taps + 1] = space:install_write_tap(
            address,
            address + 1,
            name,
            function(offset, data, mem_mask)
                write_record({
                    { "type", json_string("write") },
                    { "frame", tostring(current_frame) },
                    { "address", tostring(offset) },
                    { "data", tostring(data) },
                    { "mask", tostring(mem_mask) },
                    { "pc", tostring(read_register("PC") or 0) }
                })
            end)

        if debugger_watch then
            local action = "printf \"OPENALADDIN_WRITE PC=%08X ADDR=%08X DATA=%08X\\n\",pc,wpaddr,wpdata ; g"
            cpu.debug:wpset(space, "w", address, 2, "", action)
        end
    end
end

if trace_actors then
    for slot = 0, actor_slot_count - 1 do
        local actor_slot = slot
        local record = actor_table_base + actor_slot * actor_stride
        local type_address = record + actor_type_offset
        local animation_pc_address = record + actor_animation_pc_offset

        watched_addresses[#watched_addresses + 1] = animation_pc_address
        watch_taps[#watch_taps + 1] = space:install_write_tap(
            animation_pc_address,
            animation_pc_address + 3,
            string.format("openaladdin_actor_%02d_animation_pc", actor_slot),
            function(offset, data, mem_mask)
                write_record({
                    { "type", json_string("actor_write") },
                    { "frame", tostring(current_frame) },
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
            end)

        watched_addresses[#watched_addresses + 1] = type_address
        watch_taps[#watch_taps + 1] = space:install_write_tap(
            type_address,
            type_address + 1,
            string.format("openaladdin_actor_%02d_type", actor_slot),
            function(offset, data, mem_mask)
                write_record({
                    { "type", json_string("actor_write") },
                    { "frame", tostring(current_frame) },
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
            cpu.debug:wpset(
                space,
                "w",
                animation_pc_address,
                4,
                "",
                animation_action)
        end
    end
end

if trace_actor_initializers then
    local initializer_action =
        "printf \"OPENALADDIN_ACTOR_INIT DEST=%08X SOURCE=%08X PC=%08X RETURN=%08X\\n\",a5,a6,pc,d@sp ; g"
    cpu.debug:bpset(0x1ae30a, "", initializer_action)
end

if vdp_device and capture_vdp then
    local function install_vdp_tap(base, suffix)
        vdp_taps[#vdp_taps + 1] = space:install_write_tap(
            base,
            base + 0x1f,
            "openaladdin_vdp_writes_" .. suffix,
            function(offset, data, mem_mask)
                vdp_writes:write(json_object({
                    { "frame", tostring(current_frame) },
                    { "address", tostring(offset) },
                    { "data", tostring(data & 0xffff) },
                    { "mask", tostring(mem_mask & 0xffff) },
                    { "pc", tostring(read_register("PC") or 0) }
                }), "\n")
                vdp_writes:flush()
            end)
    end
    install_vdp_tap(0xc00000, "c00000")
    install_vdp_tap(0xd00000, "d00000")
end

local function inject_actor(frame)
    if frame ~= inject_actor_frame then
        return
    end
    if inject_actor_slot < 0 or inject_actor_slot >= actor_slot_count then
        error("OPENALADDIN_INJECT_ACTOR_SLOT is outside the traced actor table")
    end

    local record = actor_table_base + inject_actor_slot * actor_stride
    local x = inject_actor_x >= 0 and inject_actor_x or read_u16(0xff7dfa)
    local y = inject_actor_y >= 0 and inject_actor_y or read_u16(0xff7dfc)

    for offset = 0, actor_stride - 1 do
        space:write_u8(record + offset, space:read_u8(inject_actor_template + offset))
    end
    space:write_u8(record + actor_type_offset, inject_actor_type & 0xff)
    space:write_u16(record + 0x02, x & 0xffff)
    space:write_u16(record + 0x04, y & 0xffff)
    space:write_u32(record + actor_animation_pc_offset, inject_actor_pc & 0xffffffff)
    space:write_u8(record + 0x37, 0)

    print(string.format(
        "OpenAladdin: injected actor slot %d type %02X pc %08X at frame %d",
        inject_actor_slot,
        inject_actor_type & 0xff,
        inject_actor_pc & 0xffffffff,
        frame))
end

local function port_tags_json()
    local tags = {}
    for tag in pairs(machine.ioport.ports) do
        tags[#tags + 1] = json_string(tag)
    end
    table.sort(tags)
    return json_array(tags)
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
    { "ram_start", tostring(ram_start) },
    { "ram_size", tostring(ram_size) },
    { "reset_ssp", tostring(read_u32(0)) },
    { "reset_pc", tostring(read_u32(4)) },
    { "vdp_device", vdp_device and json_string(vdp_device.tag) or "null" },
    { "vdp_vram_bytes", tostring(capture_vdp and vdp_vram_item and 0x10000 or 0) },
    { "vdp_cram_bytes", tostring(capture_vdp and vdp_cram_item and 0x80 or 0) },
    { "vdp_vsram_bytes", tostring(capture_vdp and vdp_vsram_item and 0x80 or 0) },
    { "vdp_regs_bytes", tostring(capture_vdp and vdp_regs_item and 0x40 or 0) },
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
    { "actor_movement_pc_offset", tostring(actor_movement_pc_offset) },
    { "actor_animation_pc_offset", tostring(actor_animation_pc_offset) },
    { "actor_initializer_trace", json_bool(trace_actor_initializers) },
    { "actor_injection_frame", tostring(inject_actor_frame) },
    { "actor_injection_slot", tostring(inject_actor_slot) },
    { "actor_injection_type", tostring(inject_actor_type) },
    { "actor_injection_pc", tostring(inject_actor_pc) },
    { "actor_injection_template", tostring(inject_actor_template) }
})

inject_actor(0)
capture(0, apply_input(0))
capture_artifacts(0)

emu.register_frame_done(function ()
    current_frame = current_frame + 1
    if current_frame > frame_limit then
        trace:close()
        ram:close()
        vdp_vram:close()
        vdp_cram:close()
        vdp_vsram:close()
        vdp_regs:close()
        vdp_writes:close()
        machine:exit()
        return
    end

    inject_actor(current_frame)
    capture(current_frame, apply_input(current_frame))
    capture_artifacts(current_frame)

    if current_frame == frame_limit then
        trace:close()
        ram:close()
        vdp_vram:close()
        vdp_cram:close()
        vdp_vsram:close()
        vdp_regs:close()
        vdp_writes:close()
        machine:exit()
    end
end)
