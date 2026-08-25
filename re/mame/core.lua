-- Shared MAME state, symbol, JSON, and memory helpers.

local root = os.getenv("OPENALADDIN_ROOT") or "."
local machine = manager.machine
local cpu = machine.devices[":maincpu"]
if not cpu then
    error("MAME did not expose :maincpu")
end

local space = cpu.spaces["program"]
if not space then
    error("MAME main CPU has no program address space")
end

local symbols = dofile(root .. "/re/mame/bootstrap.lua")

local function symbol(name)
    local value = symbols[name]
    if value == nil then
        error("missing generated MAME symbol: " .. name .. "; run python tools/import-rom.py")
    end
    return value
end

local function env_number(name, fallback)
    local value = tonumber(os.getenv(name) or "")
    return value == nil and fallback or value
end

local function join_path(dir, name)
    return dir:sub(-1) == "/" and dir .. name or dir .. "/" .. name
end

local function json_escape(value)
    value = tostring(value)
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
    return "[" .. table.concat(values, ",") .. "]"
end

local function json_object(fields)
    local result = {}
    for _, field in ipairs(fields) do
        result[#result + 1] = json_string(field[1]) .. ":" .. field[2]
    end
    return "{" .. table.concat(result, ",") .. "}"
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

local function signed_u16(value)
    value = value & 0xffff
    return value >= 0x8000 and value - 0x10000 or value
end

local function read_register(name)
    local entry = cpu.state[name]
    return entry and entry.value or nil
end

local function find_device(tag, shortname)
    local device = machine.devices[tag]
    if device then
        return device
    end
    for candidate_tag, candidate in pairs(machine.devices) do
        if (shortname and candidate.shortname == shortname)
            or candidate_tag:find("gen_vdp", 1, true) ~= nil then
            return candidate
        end
    end
    return nil
end

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
    return index == nil and nil or emu.item(index)
end

return {
    root = root,
    machine = machine,
    cpu = cpu,
    space = space,
    symbols = symbols,
    symbol = symbol,
    env_number = env_number,
    join_path = join_path,
    json_escape = json_escape,
    json_string = json_string,
    json_bool = json_bool,
    json_array = json_array,
    json_object = json_object,
    read_u8 = read_u8,
    read_u16 = read_u16,
    read_u32 = read_u32,
    signed_u16 = signed_u16,
    read_register = read_register,
    find_device = find_device,
    find_save_item = find_save_item
}
