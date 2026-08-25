-- Optional runtime observation of indirect dispatch targets.
--
-- The probe sets breakpoints on the targets already present in the canonical
-- terrain and actor-VM pointer tables.  MAME writes the actual target and the
-- return address of the indirect call to debug.log.  This is intentionally
-- disabled for normal traces because debugger breakpoints add overhead.

return function(options)
    local cpu = options.cpu
    local symbol = options.symbol
    local read_u32 = options.read_u32

    local enabled = os.getenv("OPENALADDIN_TRACE_EDGES") == "1"
    if not enabled then
        return {
            enabled = false,
            target_count = 0,
            table_count = 0
        }
    end
    if not cpu.debug then
        error("OPENALADDIN_TRACE_EDGES requires MAME debugger support")
    end

    local table_specs = {
        {
            name = "terrain",
            address = symbol("TERRAIN_RESPONSE_HANDLER_TABLE"),
            count = 72
        },
        {
            name = "actor_vm",
            address = symbol("ACTOR_VM_DISPATCH_TABLE"),
            count = 21
        },
        {
            name = "player_collision",
            address = symbol("PLAYER_COLLISION_HANDLER_TABLE"),
            count = 256
        },
        {
            name = "actor_collision",
            address = symbol("ACTOR_COLLISION_HANDLER_TABLE"),
            count = 256
        },
        {
            name = "interaction",
            address = symbol("INTERACTION_HANDLER_TABLE"),
            count = 256
        }
    }
    local targets = {}
    local target_order = {}
    local function add_target(table_name, target)
        -- The fixed USA ROM is 2 MiB; values outside the ROM are data, not
        -- executable handler destinations.
        if target <= 0 or target >= 0x200000 or (target & 1) ~= 0 then
            return
        end
        local entry = targets[target]
        if not entry then
            entry = { tables = {} }
            targets[target] = entry
            target_order[#target_order + 1] = target
        end
        entry.tables[table_name] = true
    end

    for _, table in ipairs(table_specs) do
        for index = 0, table.count - 1 do
            add_target(table.name, read_u32(table.address + index * 4))
        end
    end

    table.sort(target_order)
    for _, target in ipairs(target_order) do
        local names = {}
        for name in pairs(targets[target].tables) do
            names[#names + 1] = name
        end
        table.sort(names)
        local action = string.format(
            "printf \"OPENALADDIN_EDGE TABLE=%s TARGET=%08X RETURN=%%08X FRAME=%%08X\\n\",d@sp,frame ; g",
            table.concat(names, "+"),
            target)
        cpu.debug:bpset(target, "", action)
    end

    print(string.format(
        "OpenAladdin: indirect edge tracing enabled for %d target(s) from %d table(s)",
        #target_order,
        #table_specs))
    return {
        enabled = true,
        target_count = #target_order,
        table_count = #table_specs
    }
end
