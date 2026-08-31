#pragma once

#include "core/frame.hpp"

#include <cstdint>

namespace openaladdin::core {

enum class CameraScrollDeltaProfile : std::uint8_t {
    Full,
    Reduced,
    Tail,
};

// These callbacks publish a ROM cursor into Genesis RAM. The delta data is
// not copied into a native camera object.
void camera_select_scroll_delta_profile(
    GenesisRam& ram,
    CameraScrollDeltaProfile profile
);
void camera_select_scroll_delta_profile(
    CoreRuntime& core,
    CameraScrollDeltaProfile profile
);

// Shared level-camera consumer used by the callbacks whose scroll data is
// driven by CAMERA_SCROLL_DATA_CURSOR. A nonzero signed word advances the
// cursor by one word and is published to ACTOR_RENDER_X_OFFSET.
std::int16_t camera_consume_scroll_delta(CoreRuntime& core);

// Camera_UpdateFollow at the recovered frame boundary.
void camera_update_follow(CoreRuntime& core);

// Camera_PublishScroll and its four directional refill paths. Refill returns
// true when the ROM path crossed its deadband/bounds checks and serviced the
// corresponding interaction window.
void camera_publish_scroll(CoreRuntime& core, CoreTrace* trace = nullptr);
bool camera_scroll_left_and_refill(CoreRuntime& core, CoreTrace* trace = nullptr);
bool camera_scroll_right_and_refill(CoreRuntime& core, CoreTrace* trace = nullptr);
bool camera_scroll_down_and_refill(CoreRuntime& core, CoreTrace* trace = nullptr);
bool camera_scroll_up_and_refill(CoreRuntime& core, CoreTrace* trace = nullptr);

}  // namespace openaladdin::core
