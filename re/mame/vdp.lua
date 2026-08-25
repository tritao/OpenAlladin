-- Genesis VDP inspection and optional per-frame memory capture.

return function(options)
    local core = options.core
    local json_array = core.json_array
    local json_object = core.json_object
    local json_string = core.json_string
    local item_word = function(item, index)
        return item and ((item:read(index) or 0) & 0xffff) or 0
    end

    local vdp = {
        capture = options.capture,
        device = core.find_device(":gen_vdp", "sega315_5313"),
        vram = options.vram,
        cram = options.cram,
        vsram = options.vsram,
        regs = options.regs,
        writes = options.writes,
        address = options.address,
        code = options.code,
        command_pending = options.command_pending,
        current_frame = options.current_frame,
        read_register = options.read_register
    }

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

    function vdp.registers_json()
        if not vdp.regs then return "[]" end
        local values = {}
        for index = 0, 31 do values[#values + 1] = tostring(item_word(vdp.regs, index) & 0xff) end
        return "[" .. table.concat(values, ",") .. "]"
    end

    function vdp.state_json()
        if not vdp.capture or not vdp.device then return "null" end
        return json_object({
            { "device", json_string(vdp.device.tag) },
            { "address", tostring(item_word(vdp.address, 0)) },
            { "code", tostring(item_word(vdp.code, 0)) },
            { "command_pending", tostring(item_word(vdp.command_pending, 0)) },
            { "registers", vdp.registers_json() },
            { "vram_fnv1a", tostring(fnv1a_item(vdp.vram, 0x10000 / 2)) },
            { "cram_fnv1a", tostring(fnv1a_item(vdp.cram, 0x80 / 2)) },
            { "vsram_fnv1a", tostring(fnv1a_item(vdp.vsram, 0x80 / 2)) }
        })
    end

    function vdp.items_json()
        if not vdp.device then return "[]" end
        local names = {}
        for name in pairs(vdp.device.items) do names[#names + 1] = json_string(name) end
        table.sort(names)
        return json_array(names)
    end

    function vdp.dump()
        if not vdp.capture or not vdp.vram or not vdp.cram or not vdp.vsram or not vdp.regs then return end
        local function dump(item, count, output)
            local chunks = {}
            for index = 0, count - 1 do chunks[#chunks + 1] = string.pack(">I2", item_word(item, index)) end
            output:write(table.concat(chunks))
            output:flush()
        end
        dump(vdp.vram, 0x10000 / 2, vdp.vram_output)
        dump(vdp.cram, 0x80 / 2, vdp.cram_output)
        dump(vdp.vsram, 0x80 / 2, vdp.vsram_output)
        dump(vdp.regs, 0x40 / 2, vdp.regs_output)
    end

    function vdp.set_outputs(outputs)
        vdp.vram_output = outputs.vram
        vdp.cram_output = outputs.cram
        vdp.vsram_output = outputs.vsram
        vdp.regs_output = outputs.regs
    end

    function vdp.install_taps(space, base)
        if not vdp.capture or not vdp.writes then return {} end
        local taps = {}
        local function install(address, suffix)
            taps[#taps + 1] = space:install_write_tap(
                address,
                address + 0x1f,
                "openaladdin_vdp_writes_" .. suffix,
                function(offset, data, mem_mask)
                    vdp.writes:write(json_object({
                        { "frame", tostring(vdp.current_frame()) },
                        { "address", tostring(offset) },
                        { "data", tostring(data & 0xffff) },
                        { "mask", tostring(mem_mask & 0xffff) },
                        { "pc", tostring(vdp.read_register("PC") or 0) }
                    }), "\n")
                    vdp.writes:flush()
                end)
        end
        install(base, "data")
        install(base + 0x100000, "mirror")
        return taps
    end

    function vdp.header_sizes()
        return {
            vram = vdp.capture and vdp.vram and 0x10000 or 0,
            cram = vdp.capture and vdp.cram and 0x80 or 0,
            vsram = vdp.capture and vdp.vsram and 0x80 or 0,
            regs = vdp.capture and vdp.regs and 0x40 or 0
        }
    end

    return vdp
end
