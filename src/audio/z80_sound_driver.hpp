#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace openaladdin::audio {

// The recovered Z80 driver stores sequence pointers in a 64-byte command
// queue and reads sequence streams from the 24-bit ROM address space. This
// class models that protocol without requiring a Z80 CPU core. It is the
// bridge between the recovered data format and the chip bus adapters.
class Z80SoundDriver {
public:
    static constexpr std::uint32_t kSequenceTableBase = 0x1BAF6F;
    static constexpr std::uint32_t kPatchTableBase = 0x1B9D06;
    static constexpr std::size_t kPatchStateSize = 0x27;
    static constexpr std::size_t kChannelCount = 16;
    static constexpr std::size_t kCommandQueueCapacity = 64;

    using PatchState = std::array<std::uint8_t, kPatchStateSize>;

    struct SoundEvent {
        enum class Kind {
            Note,
            Control,
        };

        Kind kind;
        std::uint8_t channel;
        std::uint8_t opcode;
        std::uint32_t stream_address;
        std::int16_t operand_a;
        std::int16_t operand_b;
        bool has_control_argument;
        std::uint8_t control_argument;
        bool has_patch_state = false;
        PatchState patch_state{};
    };

    struct ChannelState {
        bool active = false;
        std::uint8_t sound_id = 0;
        std::uint8_t track_index = 0;
        std::uint32_t stream_cursor = 0;
        std::int16_t operand_a = 0;
        std::int16_t operand_b = 0;
        std::uint16_t event_timer = 0;
        bool has_patch_state = false;
        PatchState patch_state{};
    };

    using EventHandler = std::function<void(const SoundEvent&)>;

    explicit Z80SoundDriver(std::span<const std::uint8_t> rom,
                            EventHandler event_handler = {});

    void reset();

    // Submit a command immediately, matching the command handlers in the
    // recovered driver. The queue API below is useful when mirroring the
    // original 68K-to-Z80 mailbox timing.
    void command(std::uint8_t opcode, std::span<const std::uint8_t> args);
    void enqueue_command(std::uint8_t opcode,
                         std::span<const std::uint8_t> args);

    // Advance the driver by one Z80 sound tick. At most one mailbox command
    // is consumed, followed by one stream event opportunity per active track.
    void tick();

    void set_event_handler(EventHandler event_handler) {
        event_handler_ = std::move(event_handler);
    }

    [[nodiscard]] std::size_t pending_commands() const noexcept {
        return queue_command_count_;
    }

    [[nodiscard]] const ChannelState& channel(std::size_t index) const;

private:
    static constexpr std::uint8_t kCommandMarker = 0xFF;

    static std::size_t command_argument_count(std::uint8_t opcode) noexcept;

    void process_command(std::uint8_t opcode,
                         std::span<const std::uint8_t> args);
    void start_sound(std::uint8_t sound_id);
    void stop_sound(std::uint8_t sound_id);
    void load_patch_state(ChannelState& channel, std::uint8_t patch_id);
    void process_channel(std::size_t index);
    SoundEvent read_event(std::size_t index);
    std::uint8_t read_stream_byte(ChannelState& channel);
    static std::int16_t decode_signed_operand(std::uint64_t value) noexcept;
    static bool control_has_argument(std::uint8_t opcode) noexcept;

    std::vector<std::uint8_t> rom_;
    std::uint32_t sequence_table_base_ = kSequenceTableBase;
    std::array<std::uint8_t, kCommandQueueCapacity> command_queue_{};
    std::size_t queue_read_ = 0;
    std::size_t queue_write_ = 0;
    std::size_t queue_size_ = 0;
    std::size_t queue_command_count_ = 0;
    std::uint32_t patch_table_base_ = 0;
    std::array<ChannelState, kChannelCount> channels_{};
    EventHandler event_handler_;
};

}  // namespace openaladdin::audio
