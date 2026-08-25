#include "animation.hpp"

#include <cassert>

int main() {
    using namespace openaladdin;

    PlayerAnimationVm vm;
    vm.reset();
    assert(vm.pose() == SpritePose::Idle);
    assert(vm.sprite_frame() == 201);
    assert(vm.stream_entry() == 0x00121D9A);

    // Selecting a new pose starts its first frame without consuming a tick.
    vm.update(SpritePose::Run, false);
    assert(vm.pose() == SpritePose::Run);
    assert(vm.sprite_frame() == 201);
    assert(vm.timer() == 4);
    vm.update(SpritePose::Run, false);
    vm.update(SpritePose::Run, false);
    vm.update(SpritePose::Run, false);
    assert(vm.sprite_frame() == 201);
    vm.update(SpritePose::Run, false);
    assert(vm.sprite_frame() == 202);

    vm.update(SpritePose::Run, true);
    assert(vm.facing_left());

    vm.update(SpritePose::Jump, false);
    assert(vm.pose() == SpritePose::Jump);
    assert(vm.sprite_frame() == 161);
    assert(vm.stream_entry() == 0x001221B0);
    vm.update(SpritePose::Jump, false);
    vm.update(SpritePose::Jump, false);
    vm.update(SpritePose::Jump, false);
    vm.update(SpritePose::Jump, false);
    assert(vm.sprite_frame() == 162);

    // The non-looping jump clip holds its last frame until physics selects a
    // grounded pose again.
    for (int i = 0; i < 40; ++i) {
        vm.update(SpritePose::Jump, false);
    }
    assert(vm.sprite_frame() == 165);

    vm.update(SpritePose::Landing, false);
    assert(vm.pose() == SpritePose::Landing);
    assert(vm.sprite_frame() == 171);
    for (int i = 0; i < 6; ++i) {
        vm.update(SpritePose::Landing, false);
    }
    assert(vm.sprite_frame() == 161);

    vm.update(SpritePose::Idle, false);
    assert(vm.pose() == SpritePose::Idle);
    assert(vm.sprite_frame() == 201);

    vm.update(SpritePose::Brake, false);
    assert(vm.pose() == SpritePose::Brake);
    assert(vm.sprite_frame() == 233);
    for (int i = 0; i < 40; ++i) {
        vm.update(SpritePose::Brake, false);
    }
    assert(vm.sprite_frame() == 319);
    return 0;
}
