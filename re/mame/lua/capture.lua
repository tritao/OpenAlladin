-- Profiled trace/state/raw-output streams.

return function(options)
    local core = options.core
    local join_path = core.join_path
    local json_object = core.json_object
    local read_u8 = options.read_u8

    local trace_dir = options.trace_dir
    local capture_profile = options.profile
    local capture_ram = options.capture_ram
    local capture_vdp = options.capture_vdp
    local trace_audio = options.trace_audio
    local state_output = options.state_output
    local ram_start = options.ram_start
    local ram_size = options.ram_size

    local trace = assert(io.open(join_path(trace_dir, "trace_boot.jsonl"), "wb"))
    local ram = capture_ram and assert(io.open(join_path(trace_dir, "ram_frames.bin"), "wb")) or nil
    local vdp_vram = capture_vdp and assert(io.open(join_path(trace_dir, "vdp_vram_frames.bin"), "wb")) or nil
    local vdp_cram = capture_vdp and assert(io.open(join_path(trace_dir, "vdp_cram_frames.bin"), "wb")) or nil
    local vdp_vsram = capture_vdp and assert(io.open(join_path(trace_dir, "vdp_vsram_frames.bin"), "wb")) or nil
    local vdp_regs = capture_vdp and assert(io.open(join_path(trace_dir, "vdp_regs_frames.bin"), "wb")) or nil
    local vdp_writes = capture_vdp and assert(io.open(join_path(trace_dir, "vdp_writes.jsonl"), "wb")) or nil
    local sound_writes = trace_audio and assert(io.open(join_path(trace_dir, "sound_writes.jsonl"), "wb")) or nil
    local state = state_output and assert(io.open(join_path(trace_dir, "state.jsonl"), "wb")) or nil

    local result = {
        profile = capture_profile,
        capture_ram = capture_ram,
        capture_vdp = capture_vdp,
        trace_audio = trace_audio,
        state_output = state_output,
        trace = trace,
        ram = ram,
        vdp_vram = vdp_vram,
        vdp_cram = vdp_cram,
        vdp_vsram = vdp_vsram,
        vdp_regs = vdp_regs,
        vdp_writes = vdp_writes,
        sound_writes = sound_writes,
        state = state
    }

    function result.write_record(fields)
        trace:write(json_object(fields), "\n")
        trace:flush()
    end

    function result.write_state(fields)
        if state then
            state:write(json_object(fields), "\n")
            state:flush()
        end
    end

    function result.fnv1a_ram()
        if not capture_ram then return 0 end
        local hash = 2166136261
        for offset = 0, ram_size - 1 do
            hash = hash ~ read_u8(ram_start + offset)
            hash = (hash * 16777619) & 0xffffffff
        end
        return hash
    end

    function result.dump_ram()
        if not capture_ram or not ram then return end
        local chunks = {}
        local chunk = {}
        for offset = 0, ram_size - 1 do
            chunk[#chunk + 1] = string.char(read_u8(ram_start + offset))
            if #chunk == 4096 then
                chunks[#chunks + 1] = table.concat(chunk)
                chunk = {}
            end
        end
        if #chunk > 0 then chunks[#chunks + 1] = table.concat(chunk) end
        ram:write(table.concat(chunks))
        ram:flush()
    end

    function result.close()
        if trace then trace:close() end
        if ram then ram:close() end
        if vdp_vram then vdp_vram:close() end
        if vdp_cram then vdp_cram:close() end
        if vdp_vsram then vdp_vsram:close() end
        if vdp_regs then vdp_regs:close() end
        if vdp_writes then vdp_writes:close() end
        if sound_writes then sound_writes:close() end
        if state then state:close() end
    end

    return result
end
