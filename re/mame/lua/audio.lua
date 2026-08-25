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

    return audio
end
