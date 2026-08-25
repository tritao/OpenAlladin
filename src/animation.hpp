#pragma once

#include "sprites.hpp"

#include <cstdint>
#include <vector>

namespace openaladdin {

struct AnimationStep {
    int sprite_frame = 0;
    int duration = 1;
};

// Small, data-driven player animation interpreter. The original VM has
// conditional branches, actor writes, and dynamic state calls; this slice
// intentionally executes only the recovered player pose streams needed by
// the native vertical slice.
class PlayerAnimationVm {
public:
    struct Clip {
        std::uint32_t stream_entry;
        std::vector<AnimationStep> steps;
        bool loop;
        std::size_t loop_start;
    };

    void reset();
    void update(SpritePose desired_pose, bool face_left_input);
    bool set_frame(int sprite_frame);
    bool finished() const;

    SpritePose pose() const { return pose_; }
    int sprite_frame() const;
    int timer() const { return timer_; }
    bool facing_left() const { return facing_left_; }

    // Original ROM stream entry for the currently selected pose. This is a
    // stream identity, not the live cursor (which is the next VM field to
    // recover once conditional control flow is implemented).
    std::uint32_t stream_entry() const;

private:
    static const Clip& clip(SpritePose pose);
    void select(SpritePose pose);

    SpritePose pose_ = SpritePose::Idle;
    std::size_t step_ = 0;
    int timer_ = 1;
    bool facing_left_ = false;
};

}  // namespace openaladdin
