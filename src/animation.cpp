#include "animation.hpp"

#include <stdexcept>

namespace openaladdin {
namespace {

// Stable player frame sequences observed through the live actor frame
// pointer at ACTOR_TABLE_BASE + 0x14. Run advances through the extracted
// 201..214 records; jump uses the five-frame airborne sequence 161..165.
// Dwell values are from synchronized MAME RAM captures. Conditional F4/F0
// branches and dynamic F8 state selection remain outside this slice.
const PlayerAnimationVm::Clip kIdleClip{
    0x00121D9A,
    {
        {201, 34}, {282, 2}, {283, 4}, {284, 60},
        {283, 2}, {285, 4}, {286, 4}, {287, 43},
    },
    true,
    0,
};

const PlayerAnimationVm::Clip kRunClip{
    0x00122006,
    {
        {201, 4}, {202, 4}, {203, 4}, {204, 4},
        {205, 4}, {206, 2}, {207, 4}, {208, 4},
        {209, 4}, {210, 4}, {211, 2}, {212, 4},
        {213, 4}, {214, 4},
        {205, 4}, {206, 2}, {207, 4}, {208, 4}, {209, 4},
        {210, 4}, {211, 2}, {212, 4}, {213, 4}, {214, 4},
    },
    true,
    14,
};

const PlayerAnimationVm::Clip kBrakeClip{
    0x001232E0,
    {
        {233, 4}, {234, 4}, {235, 4}, {236, 4}, {237, 4},
        {238, 4}, {316, 4}, {317, 2}, {318, 4}, {319, 4},
    },
    false,
    0,
};

const PlayerAnimationVm::Clip kJumpClip{
    0x001221B0,
    {
        {161, 4}, {162, 4}, {163, 2}, {164, 2}, {165, 6},
    },
    false,
    0,
};

const PlayerAnimationVm::Clip kLandingClip{
    0x00121F84,
    {
        {171, 6}, {161, 6},
    },
    false,
    0,
};

}  // namespace

const PlayerAnimationVm::Clip& PlayerAnimationVm::clip(SpritePose pose) {
    switch (pose) {
    case SpritePose::Idle: return kIdleClip;
    case SpritePose::Run: return kRunClip;
    case SpritePose::Brake: return kBrakeClip;
    case SpritePose::Jump: return kJumpClip;
    case SpritePose::Landing: return kLandingClip;
    }
    throw std::runtime_error("unknown player animation pose");
}

void PlayerAnimationVm::reset() {
    pose_ = SpritePose::Idle;
    step_ = 0;
    timer_ = clip(pose_).steps.front().duration;
    facing_left_ = false;
}

void PlayerAnimationVm::select(SpritePose pose) {
    pose_ = pose;
    step_ = 0;
    timer_ = clip(pose_).steps.front().duration;
}

bool PlayerAnimationVm::set_frame(int sprite_frame) {
    for (SpritePose candidate : {SpritePose::Idle, SpritePose::Run, SpritePose::Brake, SpritePose::Jump, SpritePose::Landing}) {
        const Clip& selected = clip(candidate);
        for (std::size_t index = 0; index < selected.steps.size(); ++index) {
            if (selected.steps[index].sprite_frame == sprite_frame) {
                pose_ = candidate;
                step_ = index;
                timer_ = selected.steps[index].duration;
                return true;
            }
        }
    }
    return false;
}

bool PlayerAnimationVm::finished() const {
    const Clip& current = clip(pose_);
    return !current.loop && step_ + 1 == current.steps.size() && timer_ <= 1;
}

void PlayerAnimationVm::update(SpritePose desired_pose, bool face_left_input) {
    if (face_left_input) {
        facing_left_ = true;
    }

    if (desired_pose != pose_) {
        select(desired_pose);
        return;
    }

    const Clip& current = clip(pose_);
    if (timer_ > 1) {
        --timer_;
        return;
    }

    if (step_ + 1 < current.steps.size()) {
        ++step_;
    } else if (current.loop) {
        step_ = current.loop_start;
    } else {
        // Hold the terminal airborne frame until physics selects idle/run.
        timer_ = 1;
        return;
    }
    timer_ = current.steps[step_].duration;
}

int PlayerAnimationVm::sprite_frame() const {
    return clip(pose_).steps[step_].sprite_frame;
}

std::uint32_t PlayerAnimationVm::stream_entry() const {
    return clip(pose_).stream_entry;
}

}  // namespace openaladdin
