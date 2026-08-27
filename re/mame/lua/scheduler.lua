-- Focused dynamic validation for the statically recovered frame scheduler.
--
-- The addresses below mirror re/scheduler/frame_phases.yml. They are kept
-- explicit here because MAME's debugger must install breakpoints before the
-- game loop runs; the YAML remains the canonical static description.

return function(options)
    local cpu = options.cpu
    local symbol = options.symbol
    local core = options.core
    local write_record = options.write_record
    local read_u8 = options.read_u8
    local current_frame = options.current_frame

    if not cpu.debug then
        error("focused scheduler tracing requires MAME debugger support")
    end

    local call_sites = {
        { 1, 0x001A8C20, 0x001A91C6 },
        { 2, 0x001A8C24, 0x001A8E0C },
        { 3, 0x001A8C28, 0x001AD7B4 },
        { 4, 0x001A8C2C, 0x001A8E0C },
        { 5, 0x001A8C30, 0x001AD632 },
        { 6, 0x001A8C34, 0x001A986E },
        { 7, 0x001A8C38, 0x001A99F0 },
        { 8, 0x001A8C3C, 0x001ADE36 },
        { 9, 0x001A8C40, 0x001ADB5C },
        { 10, 0x001A8C44, 0x001ABB40 },
        { 11, 0x001A8C50, 0x001B321C },
        { 12, 0x001A8C62, 0x001B3212 },
        { 13, 0x001A8C74, 0x001B3226 },
        { 14, 0x001A8C86, 0x001B3230 },
        { 15, 0x001A8C92, 0x001B1E38 },
        { 16, 0x001A8C96, 0x001A9D98 },
        { 17, 0x001A8C9A, 0x001A9716 },
        { 18, 0x001A8C9E, 0x001A8E0C },
        { 19, 0x001A8CA2, 0x001AA8FA },
        { 20, 0x001A8CA6, 0x001A9304 },
        { 21, 0x001A8CAA, 0x001A9502 },
        { 22, 0x001A8CAE, 0x001ABD7E },
        { 23, 0x001A8CB2, 0x001B02EC },
        { 24, 0x001A8CB6, 0x001A8F0C },
        { 25, 0x001A8CBA, 0x001A8F04 },
        { 26, 0x001A8CBE, 0x001B00CA },
        { 27, 0x001A8CC2, 0x001B01AC },
        { 28, 0x001A8CC6, 0x001A8E0C },
        { 29, 0x001A8CCA, 0x001A8E3E },
        { 30, 0x001A8CCE, 0x001AC784 },
        { 31, 0x001A8CD2, 0x001AB7C4 },
        { 32, 0x001A8CD8, 0x001B249E },
        { 33, 0x001A8CDC, 0x001AC726 },
        { 34, 0x001A8CE0, 0x001AB776 },
        { 35, 0x001A8CE4, 0x001AE0F6 },
        { 36, 0x001A8CE8, 0x001AAA2A },
        { 37, 0x001A8CEE, 0x001B315C }
    }

    for _, call in ipairs(call_sites) do
        local action = string.format(
            "printf \"OPENALADDIN_SCHEDULER_CALL ORDINAL=%d CALL=%08X ENTRY=%08X PC=%%08X FRAME=%%08X\\n\",pc,frame ; g",
            call[1],
            call[2],
            call[3])
        cpu.debug:bpset(call[2], "", action)
    end

    local latch_specs = {
        { "FRAME_WAIT_LATCH", "FRAME_WAIT_LATCH" },
        { "VBLANK_READY_LATCH", "VBLANK_READY_LATCH" },
        { "SCENE_RESOURCE_STATUS", "SCENE_RESOURCE_STATUS" },
        { "SCENE_RESOURCE_ERROR", "SCENE_RESOURCE_ERROR" }
    }
    local latch_taps = {}
    local function writes_byte(offset, mem_mask, address)
        -- The 68000 program space is 16-bit and MAME reports the aligned
        -- word offset plus the active-byte mask. Convert that bus access back
        -- to the byte address named by the static RAM ledger.
        if (mem_mask & 0xff00) ~= 0 and offset == address then
            return true
        end
        if (mem_mask & 0x00ff) ~= 0 and offset + 1 == address then
            return true
        end
        return mem_mask == 0 and (offset == address or offset + 1 == address)
    end
    for _, latch in ipairs(latch_specs) do
        local name = latch[1]
        local address = symbol(latch[2])
        local tap_start = address & 0xfffffe
        latch_taps[#latch_taps + 1] = cpu.spaces.program:install_write_tap(
            tap_start,
            tap_start + 1,
            "openaladdin_scheduler_" .. name,
            function (offset, data, mem_mask)
                -- MAME aligns debugger watchpoints to the CPU word boundary;
                -- the write tap exposes the exact byte offset. This filter is
                -- essential for adjacent odd/even latches such as 0x7E22/23.
                if not writes_byte(offset, mem_mask, address) then
                    return
                end
                write_record({
                    { "type", core.json_string("scheduler_latch") },
                    { "name", core.json_string(name) },
                    { "address", tostring(address) },
                    { "data", tostring(data) },
                    { "mask", tostring(mem_mask) },
                    { "value", tostring(read_u8(address)) },
                    { "pc", tostring(options.read_pc()) },
                    { "frame", tostring(current_frame()) }
                })
            end)
    end

    -- The frame counter is an even byte on the 68000 bus. Keep it as a
    -- debugger watch so the instruction-level writer record remains in the
    -- same debug.log stream as the call-site breakpoints. Odd-byte latches
    -- above use exact write taps because debugger watchpoints word-align.
    local phase_address = symbol("FRAME_PHASE_COUNTER")
    local phase_action = string.format(
        "printf \"OPENALADDIN_SCHEDULER_LATCH NAME=FRAME_PHASE_COUNTER ADDR=%06X WPADDR=%%08X DATA=%%08X PC=%%08X FRAME=%%08X VALUE=%%02X\\n\",wpaddr,wpdata,pc,frame,:maincpu.b@$%06X ; g",
        phase_address,
        phase_address)
    cpu.debug:wpset(cpu.spaces.program, "w", phase_address, 1, "", phase_action)

    return {
        call_site_count = #call_sites,
        latch_count = #latch_specs,
        latch_taps = latch_taps
    }
end
