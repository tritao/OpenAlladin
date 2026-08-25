-- Optional Genesis audio-bus instrumentation.
--
-- This records writes at both sides of the Mega Drive sound path:
--   * 68000 -> YM2612 and PSG
--   * Z80   -> YM2612 and PSG
--
-- It deliberately records bus writes instead of host audio samples.  The
-- register stream is deterministic and is the useful evidence for recovering
-- the original driver, channel allocation, and command protocol.

return function(options)
    local core = options.core
    local machine = core.machine
    local main_space = options.main_space
    local main_cpu = options.main_cpu
    local writes = options.writes
    local trace_dir = options.trace_dir

    local audio = {
        enabled = writes ~= nil,
        taps = {}
    }

    local function cpu_pc(target_cpu)
        if not target_cpu or not target_cpu.state then
            return 0
        end
        local entry = target_cpu.state["PC"] or target_cpu.state["pc"]
        return entry and entry.value or 0
    end

    local function emit(kind, source, target_cpu, offset, data, mem_mask, port)
        if not writes then
            return
        end
        writes:write(core.json_object({
            { "type", core.json_string("audio_write") },
            { "frame", tostring(options.current_frame()) },
            { "kind", core.json_string(kind) },
            { "source", core.json_string(source) },
            { "address", tostring(offset) },
            { "port", tostring(port) },
            { "data", tostring(data & 0xffff) },
            { "byte", tostring(data & 0xff) },
            { "mask", tostring(mem_mask & 0xffff) },
            { "pc", tostring(cpu_pc(target_cpu)) }
        }), "\n")
        writes:flush()
    end

    local function install(space, address, size, name, kind, source, target_cpu, port_base)
        if not space then
            return
        end
        audio.taps[#audio.taps + 1] = space:install_write_tap(
            address,
            address + size - 1,
            name,
            function(offset, data, mem_mask)
                emit(kind, source, target_cpu, offset, data, mem_mask, offset - port_base)
            end)
    end

    if not audio.enabled then
        return audio
    end

    -- 68000 hardware mappings.  The PSG lives behind the VDP address map;
    -- the four aligned addresses below correspond to the byte ports $11/$13/
    -- $15/$17 seen by 68K code.
    install(main_space, 0xA04000, 4, "openaladdin_audio_68k_ym", "ym2612", "maincpu", main_cpu, 0xA04000)
    install(main_space, 0xC00010, 8, "openaladdin_audio_68k_psg", "psg", "maincpu", main_cpu, 0xC00000)
    install(main_space, 0xD00010, 8, "openaladdin_audio_68k_psg_mirror", "psg", "maincpu", main_cpu, 0xD00000)

    -- MAME's Genesis Z80 tag and mappings are stable for this driver.  Keep
    -- this optional so the trace still works if a different machine exposes
    -- no Z80 sound CPU.
    local z80 = machine.devices[":genesis_snd_z80"]
    local z80_program = z80 and z80.spaces["program"] or nil
    install(z80_program, 0x4000, 4, "openaladdin_audio_z80_ym", "ym2612", "z80", z80, 0x4000)
    install(z80_program, 0x7F00, 0x100, "openaladdin_audio_z80_psg", "psg", "z80", z80, 0x7F00)

    -- Optional runtime map for the driver's banked 68K-ROM stream pointers.
    -- The command 0x10 handler consumes a 33-byte header into $0BBD and
    -- assigns its sixteen little-endian track offsets to $1B80/$20 records.
    -- Capturing these fields is more useful than guessing the stream format
    -- from YM writes alone, and remains disabled for normal audio traces.
    local driver_output
    if os.getenv("OPENALADDIN_TRACE_AUDIO_DRIVER") == "1" and z80_program then
        driver_output = assert(io.open(core.join_path(trace_dir, "z80_driver_state.jsonl"), "wb"))
        audio.driver_output = driver_output
        audio.dump_driver = function(frame, reason)
            local function byte(address)
                return z80_program:read_u8(address) & 0xff
            end
            local function bytes(start, count)
                local values = {}
                for offset = 0, count - 1 do
                    values[#values + 1] = tostring(byte(start + offset))
                end
                return "[" .. table.concat(values, ",") .. "]"
            end
            driver_output:write(core.json_object({
                { "type", core.json_string("z80_driver_state") },
                { "frame", tostring(frame) },
                { "reason", core.json_string(reason) },
                { "pointer_table", bytes(0x0AA5, 12) },
                { "header_buffer", bytes(0x0BBD, 33) },
                { "channel_state", bytes(0x1B80, 0x20 * 16) }
            }), "\n")
            driver_output:flush()
        end
        audio.close = function()
            if driver_output then
                driver_output:close()
                driver_output = nil
            end
        end
    end

    return audio
end
