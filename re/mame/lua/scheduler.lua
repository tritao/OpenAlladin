-- Focused dynamic validation for the statically recovered frame scheduler.
--
-- The addresses below mirror re/scheduler/frame_phases.yml. They are kept
-- explicit here because MAME's debugger must install breakpoints before the
-- game loop runs; the YAML remains the canonical static description.

return function(options)
    local cpu = options.cpu
    local symbol = options.symbol

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
        { "FRAME_PHASE_COUNTER", "FRAME_PHASE_COUNTER" },
        { "SCENE_RESOURCE_STATUS", "SCENE_RESOURCE_STATUS" },
        { "SCENE_RESOURCE_ERROR", "SCENE_RESOURCE_ERROR" }
    }
    for _, latch in ipairs(latch_specs) do
        local name = latch[1]
        local address = symbol(latch[2])
        local action = string.format(
            "printf \"OPENALADDIN_SCHEDULER_LATCH NAME=%s ADDR=%06X WPADDR=%%08X DATA=%%08X PC=%%08X FRAME=%%08X VALUE=%%02X\\n\",wpaddr,wpdata,pc,frame,:maincpu.b@$%06X ; g",
            name,
            address,
            address)
        cpu.debug:wpset(cpu.spaces.program, "w", address, 1, "", action)
    end

    return { call_site_count = #call_sites }
end
