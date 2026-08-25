-- Actor-table capture metadata and the optional deterministic actor injection.

return function(options)
    local space = options.space
    local symbol = options.symbol
    local read_u16 = options.read_u16
    local actor_table_base = options.actor_table_base
    local actor_stride = options.actor_stride
    local actor_slot_count = options.actor_slot_count
    local actor_type_offset = options.actor_type_offset
    local actor_x_offset = options.actor_x_offset
    local actor_y_offset = options.actor_y_offset
    local actor_animation_pc_offset = options.actor_animation_pc_offset

    local actor = {
        table_base = actor_table_base,
        stride = actor_stride,
        slot_count = actor_slot_count,
        type_offset = actor_type_offset,
        x_offset = actor_x_offset,
        y_offset = actor_y_offset,
        movement_pc_offset = options.actor_movement_pc_offset,
        frame_ptr_offset = options.actor_frame_ptr_offset,
        animation_pc_offset = actor_animation_pc_offset
    }

    local injection_frame = options.injection_frame
    local injection_slot = options.injection_slot
    local injection_type = options.injection_type
    local injection_pc = options.injection_pc
    local injection_template = options.injection_template
    local injection_x = options.injection_x
    local injection_y = options.injection_y
    local injection_movement_pc = options.injection_movement_pc
    local injection_facing_x = options.injection_facing_x
    local injection_facing_y = options.injection_facing_y
    local injection_flags = options.injection_flags
    local injection_movement_timer = options.injection_movement_timer
    local injection_return_pc = options.injection_return_pc

    function actor.inject(frame)
        if frame ~= injection_frame then
            return
        end
        if injection_slot < 0 or injection_slot >= actor_slot_count then
            error("OPENALADDIN_INJECT_ACTOR_SLOT is outside the traced actor table")
        end

        local record = actor_table_base + injection_slot * actor_stride
        local x = injection_x >= 0 and injection_x or read_u16(symbol("PLAYER_X"))
        local y = injection_y >= 0 and injection_y or read_u16(symbol("PLAYER_Y"))

        for offset = 0, actor_stride - 1 do
            space:write_u8(record + offset, space:read_u8(injection_template + offset))
        end
        space:write_u8(record + actor_type_offset, injection_type & 0xff)
        space:write_u16(record + actor_x_offset, x & 0xffff)
        space:write_u16(record + actor_y_offset, y & 0xffff)
        space:write_u32(record + actor_animation_pc_offset, injection_pc & 0xffffffff)
        if injection_movement_pc >= 0 then
            space:write_u32(record + options.actor_movement_pc_offset, injection_movement_pc & 0xffffffff)
        end
        if injection_facing_x >= 0 then
            space:write_u8(record + 0x09, injection_facing_x & 0xff)
        end
        if injection_facing_y >= 0 then
            space:write_u8(record + 0x35, injection_facing_y & 0xff)
        end
        if injection_flags >= 0 then
            space:write_u8(record + 0x3c, injection_flags & 0xff)
        end
        if injection_movement_timer >= 0 then
            space:write_u8(record + 0x36, injection_movement_timer & 0xff)
        end
        if injection_return_pc >= 0 then
            space:write_u32(record + 0x38, injection_return_pc & 0xffffffff)
        end
        space:write_u8(record + 0x37, 0)

        print(string.format(
            "OpenAladdin: injected actor slot %d type %02X pc %08X at frame %d",
            injection_slot,
            injection_type & 0xff,
            injection_pc & 0xffffffff,
            frame))
    end

    return actor
end
