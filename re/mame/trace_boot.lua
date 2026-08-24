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

local trace = assert(io.open(trace_path, "wb"))
local ram = assert(io.open(ram_path, "wb"))

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
        { "ram_fnv1a", tostring(fnv1a_ram()) }
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
local watched_addresses = {}
local current_frame = 0
local watch_list = os.getenv("OPENALADDIN_WATCH_ADDRESSES") or ""
local debugger_watch = os.getenv("OPENALADDIN_DEBUG_WATCH") == "1"

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
    { "input_ports", port_tags_json() },
    { "player1_input_fields", input_fields_json() },
    { "watched_addresses", json_array((function ()
        local values = {}
        for index, address in ipairs(watched_addresses) do
            values[index] = tostring(address)
        end
        return values
    end)()) }
})

capture(0, apply_input(0))
capture_artifacts(0)

emu.register_frame_done(function ()
    current_frame = current_frame + 1
    if current_frame > frame_limit then
        trace:close()
        ram:close()
        machine:exit()
        return
    end

    capture(current_frame, apply_input(current_frame))
    capture_artifacts(current_frame)

    if current_frame == frame_limit then
        trace:close()
        ram:close()
        machine:exit()
    end
end)
