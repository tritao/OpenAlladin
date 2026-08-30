cmake_minimum_required(VERSION 3.20)

project(OpenAladdin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENALADDIN_BUILD_TESTS "Build OpenAladdin tests" ON)
set(OPENALADDIN_SDL2_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/sdl2/sysroot/usr"
    CACHE PATH "SDL2 installation prefix")

# The repository's native build currently uses a local SDL2 sysroot. Prefer
# its CMake package when present, but also allow a system SDL2 installation.
if(IS_DIRECTORY "${OPENALADDIN_SDL2_ROOT}")
    list(PREPEND CMAKE_PREFIX_PATH "${OPENALADDIN_SDL2_ROOT}")
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
    else()
        message(FATAL_ERROR
            "SDL2 was not found. Install SDL2 development files or set "
            "OPENALADDIN_SDL2_ROOT to an SDL2 installation prefix.")
    endif()
endif()

if(NOT SDL2_INCLUDE_DIRS)
    get_target_property(_openaladdin_sdl2_includes SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    set(SDL2_INCLUDE_DIRS "${_openaladdin_sdl2_includes}")
endif()

# Debian-style SDL2 packages keep the generated SDL_config.h in a
# multiarch include directory. The repository-local package's CMake metadata
# does not list it, although sdl2-config does, so add it when present.
if(CMAKE_LIBRARY_ARCHITECTURE
   AND IS_DIRECTORY "${OPENALADDIN_SDL2_ROOT}/include/${CMAKE_LIBRARY_ARCHITECTURE}")
    list(APPEND SDL2_INCLUDE_DIRS
        "${OPENALADDIN_SDL2_ROOT}/include/${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

# Prefer SDL2's shared target. The repository-local SDL2 sysroot contains the
# runtime sonames but not development symlinks for every transitive static
# dependency, so the static target cannot be linked on a minimal checkout.
set(OPENALADDIN_SDL2_TARGET SDL2::SDL2)

# The checked-in build environment may contain SDL2's static archive while
# its shared-library symlink is intentionally absent. Recreate the explicit
# soname link list used by the native build in that case, avoiding a dependency
# on uninstalled development symlinks such as libsamplerate.so.
if(UNIX AND NOT APPLE
   AND DEFINED SDL2_LIBDIR
   AND TARGET SDL2::SDL2-static
   AND EXISTS "${SDL2_LIBDIR}/libSDL2.a"
   AND NOT EXISTS "${SDL2_LIBDIR}/libSDL2.so")
    add_library(openaladdin_sdl2 INTERFACE)
    target_link_libraries(openaladdin_sdl2 INTERFACE
        "${SDL2_LIBDIR}/libSDL2.a"
        -lm
        "-Wl,-l:libasound.so.2"
        -ldl
        -lpthread
        "-Wl,-l:libpulse.so.0"
        "-Wl,-l:libsamplerate.so.0"
        "-Wl,-l:libX11.so.6"
        "-Wl,-l:libXext.so.6"
        "-Wl,-l:libXcursor.so.1"
        "-Wl,-l:libXi.so.6"
        "-Wl,-l:libXfixes.so.3"
        "-Wl,-l:libXrandr.so.2"
        "-Wl,-l:libXss.so.1"
        "-Wl,-l:libdrm.so.2"
        "-Wl,-l:libgbm.so.1"
        "-Wl,-l:libwayland-egl.so.1"
        "-Wl,-l:libwayland-client.so.0"
        "-Wl,-l:libwayland-cursor.so.0"
        "-Wl,-l:libxkbcommon.so.0"
        "-Wl,-l:libdecor-0.so.0")
    set(OPENALADDIN_SDL2_TARGET openaladdin_sdl2)
endif()

function(openaladdin_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

function(openaladdin_enable_test_assertions target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -UNDEBUG)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    endif()
endfunction()

add_library(openaladdin_core STATIC
    src/animation.cpp
    src/animation.hpp
    src/game_ram.cpp
    src/game_ram.hpp
    src/player_terrain.hpp
    src/game_state.hpp
    src/sprites.cpp
    src/sprites.hpp
)
target_include_directories(openaladdin_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${SDL2_INCLUDE_DIRS}
)
target_compile_features(openaladdin_core PUBLIC cxx_std_20)
openaladdin_enable_warnings(openaladdin_core)

set(OPENALADDIN_YMFM_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/external/mame/3rdparty/ymfm/src")
if(NOT EXISTS "${OPENALADDIN_YMFM_ROOT}/ymfm_opn.cpp")
    message(FATAL_ERROR
        "ymfm was not found. Initialize the external/mame submodule before "
        "configuring the native build.")
endif()

add_library(openaladdin_ymfm STATIC
    "${OPENALADDIN_YMFM_ROOT}/ymfm_opn.cpp"
    "${OPENALADDIN_YMFM_ROOT}/ymfm_adpcm.cpp"
    "${OPENALADDIN_YMFM_ROOT}/ymfm_pcm.cpp"
    "${OPENALADDIN_YMFM_ROOT}/ymfm_ssg.cpp"
)
target_include_directories(openaladdin_ymfm PUBLIC "${OPENALADDIN_YMFM_ROOT}")
target_compile_features(openaladdin_ymfm PUBLIC cxx_std_20)

add_library(openaladdin_audio STATIC
    src/audio/sn76489.cpp
    src/audio/sn76489.hpp
    src/audio/mixer.cpp
    src/audio/mixer.hpp
    src/audio/trace.cpp
    src/audio/trace.hpp
    src/audio/z80_audio_bridge.cpp
    src/audio/z80_audio_bridge.hpp
    src/audio/ym2612.cpp
    src/audio/ym2612.hpp
    src/audio/z80_sound_driver.cpp
    src/audio/z80_sound_driver.hpp
)
target_include_directories(openaladdin_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(openaladdin_audio PUBLIC cxx_std_20)
target_link_libraries(openaladdin_audio PUBLIC openaladdin_ymfm)
openaladdin_enable_warnings(openaladdin_audio)

add_library(openaladdin_audio_sdl STATIC
    src/audio/sdl_audio.cpp
    src/audio/sdl_audio.hpp
)
target_include_directories(openaladdin_audio_sdl
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        ${SDL2_INCLUDE_DIRS}
)
target_compile_features(openaladdin_audio_sdl PUBLIC cxx_std_20)
target_link_libraries(openaladdin_audio_sdl PUBLIC
    openaladdin_audio
    ${OPENALADDIN_SDL2_TARGET}
)
openaladdin_enable_warnings(openaladdin_audio_sdl)

add_library(openaladdin_engine STATIC
    src/actor_movement.cpp
    src/actor_movement.hpp
    src/actor_terrain.cpp
    src/actor_terrain.hpp
    src/actor.cpp
    src/actor.hpp
    src/actor_animation_system.cpp
    src/actor_animation_system.hpp
    src/animation_system.cpp
    src/animation_system.hpp
    src/actor_lifecycle.cpp
    src/actor_lifecycle.hpp
    src/camera.cpp
    src/camera.hpp
    src/collision.cpp
    src/collision.hpp
    src/engine.cpp
    src/engine.hpp
    src/frame_scheduler.cpp
    src/frame_scheduler.hpp
    src/game_data.cpp
    src/game_data.hpp
    src/interaction.cpp
    src/interaction.hpp
    src/level.cpp
    src/level.hpp
    src/level_event.cpp
    src/level_event.hpp
    src/level_event_system.cpp
    src/level_event_system.hpp
    src/movement.cpp
    src/movement.hpp
    src/player_motion.cpp
    src/player_motion.hpp
    src/player_system.cpp
    src/player_system.hpp
    src/player_terrain.cpp
    src/render_pipeline.cpp
    src/render_pipeline.hpp
    src/render_model.cpp
    src/render_model.hpp
    src/scene.cpp
    src/scene.hpp
    src/scene_resource.cpp
    src/scene_resource.hpp
    src/sdl_render_backend.cpp
    src/sdl_render_backend.hpp
    src/terrain_behavior.cpp
    src/terrain_behavior.hpp
)
target_include_directories(openaladdin_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(openaladdin_engine PUBLIC cxx_std_20)
target_link_libraries(openaladdin_engine PUBLIC
    openaladdin_core
    openaladdin_audio
    ${OPENALADDIN_SDL2_TARGET}
)
openaladdin_enable_warnings(openaladdin_engine)

add_executable(openaladdin src/main.cpp)
target_compile_features(openaladdin PRIVATE cxx_std_20)
target_link_libraries(openaladdin PRIVATE
    openaladdin_engine
    openaladdin_audio_sdl
)
openaladdin_enable_warnings(openaladdin)

# Keep locally supplied SDL2 discoverable when the executable is launched
# directly after a CMake build (for example by CTest or run.sh).
if(TARGET SDL2::SDL2)
    get_target_property(_openaladdin_sdl2_location
        SDL2::SDL2 IMPORTED_LOCATION)
endif()
if(_openaladdin_sdl2_location)
    get_filename_component(_openaladdin_sdl2_library_dir
        "${_openaladdin_sdl2_location}" DIRECTORY)
    set_property(TARGET openaladdin APPEND PROPERTY BUILD_RPATH
        "${_openaladdin_sdl2_library_dir}")
endif()

include(CTest)
if(BUILD_TESTING AND OPENALADDIN_BUILD_TESTS)
    add_executable(sprite_renderer_test tests/sprite_renderer.cpp)
    target_link_libraries(sprite_renderer_test PRIVATE openaladdin_core)
    target_compile_features(sprite_renderer_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(sprite_renderer_test)
    openaladdin_enable_test_assertions(sprite_renderer_test)

    add_executable(player_animation_test tests/animation_vm.cpp)
    target_link_libraries(player_animation_test PRIVATE openaladdin_core)
    target_compile_features(player_animation_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(player_animation_test)
    openaladdin_enable_test_assertions(player_animation_test)

    add_executable(animation_services_test tests/animation_services.cpp)
    target_link_libraries(animation_services_test PRIVATE openaladdin_engine)
    target_compile_features(animation_services_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(animation_services_test)
    openaladdin_enable_test_assertions(animation_services_test)

    add_executable(level_descriptor_test tests/level_descriptor.cpp)
    target_link_libraries(level_descriptor_test PRIVATE openaladdin_engine)
    target_compile_features(level_descriptor_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(level_descriptor_test)
    openaladdin_enable_test_assertions(level_descriptor_test)

    add_executable(movement_vm_test tests/movement_vm.cpp)
    target_link_libraries(movement_vm_test PRIVATE openaladdin_engine)
    target_compile_features(movement_vm_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(movement_vm_test)
    openaladdin_enable_test_assertions(movement_vm_test)

    add_executable(level_event_vm_test tests/level_event_vm.cpp)
    target_link_libraries(level_event_vm_test PRIVATE openaladdin_engine)
    target_compile_features(level_event_vm_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(level_event_vm_test)
    openaladdin_enable_test_assertions(level_event_vm_test)

    add_executable(scene_system_test tests/scene_system.cpp)
    target_link_libraries(scene_system_test PRIVATE openaladdin_engine)
    target_compile_features(scene_system_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(scene_system_test)
    openaladdin_enable_test_assertions(scene_system_test)

    add_executable(scene_resource_test tests/scene_resource.cpp)
    target_link_libraries(scene_resource_test PRIVATE openaladdin_engine)
    target_compile_features(scene_resource_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(scene_resource_test)
    openaladdin_enable_test_assertions(scene_resource_test)

    add_executable(actor_system_test tests/actor_system.cpp)
    target_link_libraries(actor_system_test PRIVATE openaladdin_engine)
    target_compile_features(actor_system_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(actor_system_test)
    openaladdin_enable_test_assertions(actor_system_test)

    add_executable(collision_system_test tests/collision_system.cpp)
    target_link_libraries(collision_system_test PRIVATE openaladdin_engine)
    target_compile_features(collision_system_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(collision_system_test)
    openaladdin_enable_test_assertions(collision_system_test)

    add_executable(checkpoint_state_test tests/checkpoint_state.cpp)
    target_link_libraries(checkpoint_state_test PRIVATE openaladdin_engine)
    target_compile_features(checkpoint_state_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(checkpoint_state_test)
    openaladdin_enable_test_assertions(checkpoint_state_test)

    add_executable(game_ram_test tests/game_ram.cpp)
    target_link_libraries(game_ram_test PRIVATE openaladdin_engine)
    target_compile_features(game_ram_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(game_ram_test)
    openaladdin_enable_test_assertions(game_ram_test)

    add_executable(interaction_state_test tests/interaction_state.cpp)
    target_link_libraries(interaction_state_test PRIVATE openaladdin_engine)
    target_compile_features(interaction_state_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(interaction_state_test)
    openaladdin_enable_test_assertions(interaction_state_test)

    add_executable(player_terrain_test tests/player_terrain.cpp)
    target_link_libraries(player_terrain_test PRIVATE openaladdin_engine)
    target_compile_features(player_terrain_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(player_terrain_test)
    openaladdin_enable_test_assertions(player_terrain_test)

    add_executable(camera_system_test tests/camera_system.cpp)
    target_link_libraries(camera_system_test PRIVATE openaladdin_engine)
    target_compile_features(camera_system_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(camera_system_test)
    openaladdin_enable_test_assertions(camera_system_test)

    add_executable(render_model_test tests/render_model.cpp)
    target_link_libraries(render_model_test PRIVATE openaladdin_engine)
    target_compile_features(render_model_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(render_model_test)
    openaladdin_enable_test_assertions(render_model_test)

    add_executable(native_actor_vm_fixture tests/native_actor_vm_fixture.cpp)
    target_link_libraries(native_actor_vm_fixture PRIVATE openaladdin_engine)
    target_compile_features(native_actor_vm_fixture PRIVATE cxx_std_20)
    openaladdin_enable_warnings(native_actor_vm_fixture)
    openaladdin_enable_test_assertions(native_actor_vm_fixture)

    add_executable(psg_test tests/psg.cpp)
    target_link_libraries(psg_test PRIVATE openaladdin_audio)
    target_compile_features(psg_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(psg_test)
    openaladdin_enable_test_assertions(psg_test)

    add_executable(ym2612_test tests/ym2612.cpp)
    target_link_libraries(ym2612_test PRIVATE openaladdin_audio)
    target_compile_features(ym2612_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(ym2612_test)
    openaladdin_enable_test_assertions(ym2612_test)

    add_executable(mixer_test tests/mixer.cpp)
    target_link_libraries(mixer_test PRIVATE openaladdin_audio)
    target_compile_features(mixer_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(mixer_test)
    openaladdin_enable_test_assertions(mixer_test)

    add_executable(sdl_audio_test tests/sdl_audio.cpp)
    target_link_libraries(sdl_audio_test PRIVATE openaladdin_audio_sdl)
    target_compile_features(sdl_audio_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(sdl_audio_test)
    openaladdin_enable_test_assertions(sdl_audio_test)

    add_executable(z80_sound_driver_test tests/z80_sound_driver.cpp)
    target_link_libraries(z80_sound_driver_test PRIVATE openaladdin_audio)
    target_compile_features(z80_sound_driver_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(z80_sound_driver_test)
    openaladdin_enable_test_assertions(z80_sound_driver_test)

    add_executable(z80_audio_bridge_test tests/z80_audio_bridge.cpp)
    target_link_libraries(z80_audio_bridge_test PRIVATE openaladdin_audio)
    target_compile_features(z80_audio_bridge_test PRIVATE cxx_std_20)
    openaladdin_enable_warnings(z80_audio_bridge_test)
    openaladdin_enable_test_assertions(z80_audio_bridge_test)

    add_test(NAME sprite_renderer COMMAND sprite_renderer_test)
    set_tests_properties(sprite_renderer PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME player_animation COMMAND player_animation_test)
    set_tests_properties(player_animation PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME animation_services COMMAND animation_services_test)
    set_tests_properties(animation_services PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME level_descriptor COMMAND level_descriptor_test)
    set_tests_properties(level_descriptor PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME movement_vm COMMAND movement_vm_test)
    set_tests_properties(movement_vm PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME level_event_vm COMMAND level_event_vm_test)
    set_tests_properties(level_event_vm PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME scene_system COMMAND scene_system_test)
    set_tests_properties(scene_system PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME scene_resource COMMAND scene_resource_test)
    set_tests_properties(scene_resource PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME actor_system COMMAND actor_system_test)
    set_tests_properties(actor_system PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME collision_system COMMAND collision_system_test)
    set_tests_properties(collision_system PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME checkpoint_state COMMAND checkpoint_state_test)
    set_tests_properties(checkpoint_state PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME game_ram COMMAND game_ram_test)
    set_tests_properties(game_ram PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME interaction_state COMMAND interaction_state_test)
    set_tests_properties(interaction_state PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME player_terrain COMMAND player_terrain_test)
    set_tests_properties(player_terrain PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME camera_system COMMAND camera_system_test)
    set_tests_properties(camera_system PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME render_model COMMAND render_model_test)
    set_tests_properties(render_model PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME psg COMMAND psg_test)
    set_tests_properties(psg PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME ym2612 COMMAND ym2612_test)
    set_tests_properties(ym2612 PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME mixer COMMAND mixer_test)
    set_tests_properties(mixer PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME sdl_audio COMMAND sdl_audio_test)
    set_tests_properties(sdl_audio PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME z80_sound_driver COMMAND z80_sound_driver_test)
    set_tests_properties(z80_sound_driver PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME z80_audio_bridge COMMAND z80_audio_bridge_test)
    set_tests_properties(z80_audio_bridge PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        set(_openaladdin_python_environment
            "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/tools")

        add_test(
            NAME coverage_gaps
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/coverage_gaps.py")
        set_tests_properties(coverage_gaps PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME runtime_coverage
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_coverage.py")
        set_tests_properties(runtime_coverage PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME mame_headless
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/mame_headless.py")
        set_tests_properties(mame_headless PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME audio_parity
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_parity.py")
        set_tests_properties(audio_parity PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_audio_trace
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_audio_trace.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_audio_trace PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_framebuffer
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_framebuffer.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_framebuffer PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_jump_visual
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_jump_visual.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_jump_visual PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_input_trace
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_input_trace.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_input_trace PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_visual_layers
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_visual_layers.py")
        set_tests_properties(native_visual_layers PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_level_loader
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_level_loader.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_level_loader PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_sprites
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_sprites.py")
        set_tests_properties(native_sprites PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME player_jump_regression
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/player_jump_regression.py")
        set_tests_properties(player_jump_regression PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME player_run_regression
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/player_run_regression.py")
        set_tests_properties(player_run_regression PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME terrain_jump_regression
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/terrain_jump_regression.py")
        set_tests_properties(terrain_jump_regression PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_connector_response
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_connector_response.py")
        set_tests_properties(native_connector_response PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_actor_collision
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_actor_collision.py")
        set_tests_properties(native_actor_actor_collision PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_first_guard_sword
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_first_guard_sword.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_first_guard_sword PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_animation_spawn
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_animation_spawn.py")
        set_tests_properties(native_animation_spawn PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_render
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_render.py")
        set_tests_properties(native_actor_render PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_animation_clear
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_animation_clear.py")
        set_tests_properties(native_actor_animation_clear PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_type34
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_type34.py")
        set_tests_properties(native_actor_type34 PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_vm_commands
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_vm_commands.py"
                    "$<TARGET_FILE:native_actor_vm_fixture>")
        set_tests_properties(native_actor_vm_commands PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_actor_movement
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_actor_movement.py"
                    "$<TARGET_FILE:native_actor_vm_fixture>")
        set_tests_properties(native_actor_movement PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_interaction_spawner
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_interaction_spawner.py")
        set_tests_properties(native_interaction_spawner PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_sword_lifecycle
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_sword_lifecycle.py")
        set_tests_properties(native_sword_lifecycle PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME native_scheduler_trace
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    SDL_VIDEODRIVER=dummy
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_scheduler_trace.py"
                    "$<TARGET_FILE:openaladdin>")
        set_tests_properties(native_scheduler_trace PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME scheduler_compare
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/scheduler_compare.py")
        set_tests_properties(scheduler_compare PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(
            NAME scheduler_model
            COMMAND ${CMAKE_COMMAND} -E env ${_openaladdin_python_environment}
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/scheduler_model.py")
        set_tests_properties(scheduler_model PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
