"""State-trace parsing, synchronization, and normalization services."""

from __future__ import annotations

import json
from pathlib import Path
import re
from typing import Any

from genie.runtime import *
def load_state_trace(path: Path) -> tuple[dict[str, Any], dict[int, dict[str, Any]], list[dict[str, Any]]]:
    header: dict[str, Any] | None = None
    states: dict[int, dict[str, Any]] = {}
    markers: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if record.get("type") == "header":
            header = record
        elif record.get("type") == "marker":
            markers.append(record)
        elif record.get("type") in (None, "state", "frame_state"):
            if "frame" in record:
                states[int(record["frame"])] = record
    if header is None:
        raise SystemExit(f"{path}: state trace has no header")
    if not states:
        raise SystemExit(f"{path}: state trace has no state records")
    return header, states, markers

SYNC_PATTERN = re.compile(
    r"OPENALADDIN_SYNC frame=(?P<frame>\d+) pc=(?P<pc>[0-9A-F]+) "
    r"x=(?P<x>[0-9A-F]+) y=(?P<y>[0-9A-F]+) "
    r"wx=(?P<world_x>[0-9A-F]+) wy=(?P<world_y>[0-9A-F]+) "
    r"vx=(?P<vx>[0-9A-F]+) vy=(?P<vy>[0-9A-F]+) "
    r"grounded=(?P<grounded>[0-9A-F]+) "
    r"frameptr=(?P<frame_ptr>[0-9A-F]+) facing=(?P<facing_x_flip>[0-9A-F]+) "
    r"animpc=(?P<animation_pc>[0-9A-F]+) "
    r"animtimer=(?P<animation_timer>[0-9A-F]+) "
    r"camx=(?P<camera_x>[0-9A-F]+) camy=(?P<camera_y>[0-9A-F]+) "
    r"refx=(?P<reference_x>[0-9A-F]+) refy=(?P<reference_y>[0-9A-F]+) "
    r"sx=(?P<scroll_x>[0-9A-F]+) sy=(?P<scroll_y>[0-9A-F]+) "
    r"thx=(?P<horizontal_threshold>[0-9A-F]+) "
    r"thy=(?P<vertical_threshold>[0-9A-F]+) "
    r"delay=(?P<update_delay>[0-9A-F]+) special=(?P<special_mode>[0-9A-F]+) "
    r"selgate=(?P<animation_gate>[0-9A-F]+) "
    r"selterminal=(?P<terminal_transition>[0-9A-F]+) "
    r"selcountdown=(?P<scene_script_countdown>[0-9A-F]+) "
    r"sellock=(?P<interaction_lock>[0-9A-F]+) "
    r"selresponse=(?P<response_active>[0-9A-F]+) "
    r"sellanding=(?P<landing_state>[0-9A-F]+) "
    r"selgate2=(?P<transition_gate>[0-9A-F]+) "
    r"seltranslock=(?P<transition_lock>[0-9A-F]+) "
    r"selstate=(?P<transition_state>[0-9A-F]+) "
    r"selmode=(?P<transition_mode>[0-9A-F]+) "
    r"selflag=(?P<transition_flag>[0-9A-F]+) "
    r"seltransresponse=(?P<transition_response>[0-9A-F]+) "
    r"selde=(?P<transition_state_de>[0-9A-F]+) "
    r"seldf=(?P<transition_state_df>[0-9A-F]+) "
    r"selspecial=(?P<camera_special_mode>[0-9A-F]+) "
    r"sellatch=(?P<response_latch>[0-9A-F]+) "
    r"selanimation=(?P<response_animation>[0-9A-F]+) "
    r"selee=(?P<response_state_ee>[0-9A-F]+) "
    r"selef=(?P<response_state_ef>[0-9A-F]+) "
    r"self0=(?P<response_state_f0>[0-9A-F]+) "
    r"sel101=(?P<response_state_101>[0-9A-F]+) "
    r"selhresponse=(?P<horizontal_response>[0-9A-F]+) "
    r"seltimer=(?P<response_timer>[0-9A-F]+) "
    r"selpending=(?P<interaction_pending>[0-9A-F]+) "
    r"sellock2=(?P<state_lock>[0-9A-F]+)"
)

SYNC_V2_FIELDS = (
    ("frame", "frame", "decimal"),
    ("pc", "pc", "hex"),
    ("phase", "frame_phase", "hex"),
    ("x", "x", "hex"),
    ("y", "y", "hex"),
    ("wx", "world_x", "hex"),
    ("wy", "world_y", "hex"),
    ("vx", "vx", "hex"),
    ("vy", "vy", "hex"),
    ("grounded", "grounded", "hex"),
    ("frameptr", "frame_ptr", "hex"),
    ("facing", "facing_x_flip", "hex"),
    ("animpc", "animation_pc", "hex"),
    ("animtimer", "animation_timer", "hex"),
    ("camx", "camera_x", "hex"),
    ("camy", "camera_y", "hex"),
    ("refx", "reference_x", "hex"),
    ("refy", "reference_y", "hex"),
    ("sx", "scroll_x", "hex"),
    ("sy", "scroll_y", "hex"),
    ("thx", "horizontal_threshold", "hex"),
    ("thy", "vertical_threshold", "hex"),
    ("delay", "update_delay", "hex"),
    ("special", "special_mode", "hex"),
    ("pixelx", "pixel_x", "hex"),
    ("pixely", "pixel_y", "hex"),
    ("tilex", "tile_x", "hex"),
    ("tiley", "tile_y", "hex"),
    ("levelw", "level_width", "hex"),
    ("levelh", "level_height", "hex"),
    ("pendleft", "scroll_left_pending", "hex"),
    ("pendright", "scroll_right_pending", "hex"),
    ("pendup", "scroll_up_pending", "hex"),
    ("penddown", "scroll_down_pending", "hex"),
    ("selgate", "animation_gate", "hex"),
    ("selterminal", "terminal_transition", "hex"),
    ("selcountdown", "scene_script_countdown", "hex"),
    ("sellock", "interaction_lock", "hex"),
    ("selresponse", "response_active", "hex"),
    ("sellanding", "landing_state", "hex"),
    ("selgate2", "transition_gate", "hex"),
    ("seltranslock", "transition_lock", "hex"),
    ("selstate", "transition_state", "hex"),
    ("selmode", "transition_mode", "hex"),
    ("selflag", "transition_flag", "hex"),
    ("seltransresponse", "transition_response", "hex"),
    ("selde", "transition_state_de", "hex"),
    ("seldf", "transition_state_df", "hex"),
    ("selspecial", "camera_special_mode", "hex"),
    ("sellatch", "response_latch", "hex"),
    ("selanimation", "response_animation", "hex"),
    ("selee", "response_state_ee", "hex"),
    ("selef", "response_state_ef", "hex"),
    ("self0", "response_state_f0", "hex"),
    ("sel101", "response_state_101", "hex"),
    ("selhresponse", "horizontal_response", "hex"),
    ("seltimer", "response_timer", "hex"),
    ("selpending", "interaction_pending", "hex"),
    ("sellock2", "state_lock", "hex"),
    ("actorflags", "actor_flags", "hex"),
    ("attacktimer", "attack_timer", "hex"),
    ("terrwx", "terrain_world_x", "hex"),
    ("terrwy", "terrain_world_y", "hex"),
    ("terrcallbacka", "terrain_callback_a", "hex"),
    ("terrcallbackb", "terrain_callback_b", "hex"),
    ("terrcallbackc", "terrain_callback_c", "hex"),
    ("terrquery", "terrain_query_result", "hex"),
    ("terrpushright", "terrain_push_right", "hex"),
    ("terrpushleft", "terrain_push_left", "hex"),
    ("terrpushup", "terrain_push_up", "hex"),
    ("terrpushdown", "terrain_push_down", "hex"),
    ("terrbehavior", "terrain_behavior", "hex"),
    ("terrhresponse", "terrain_horizontal_response", "hex"),
    ("terractive", "terrain_response_active", "hex"),
    ("terrverticalstop", "terrain_vertical_stop", "hex"),
    ("terrelanding", "terrain_landing_state", "hex"),
    ("terrsurfacemode", "terrain_surface_mode", "hex"),
    ("terrsurfacelatch", "terrain_surface_latch", "hex"),
    ("terrsurfacetransition", "terrain_surface_transition_flag", "hex"),
    ("terrstopleft", "terrain_stop_left_motion", "hex"),
    ("terrleftinner", "terrain_left_inner_probe", "hex"),
    ("terrleftouter", "terrain_left_outer_probe", "hex"),
    ("terrstopright", "terrain_stop_right_motion", "hex"),
    ("terrrightinner", "terrain_right_inner_probe", "hex"),
    ("terrrightouter", "terrain_right_outer_probe", "hex"),
    ("terrstopup", "terrain_stop_upward_motion", "hex"),
    ("terrjumpcounter", "terrain_jump_response_counter", "hex"),
    ("terrresponsetimer", "terrain_response_timer_state", "hex"),
    ("terrstatea", "terrain_query_state_a", "hex"),
    ("terrstateb", "terrain_query_state_b", "hex"),
    ("terrstate", "terrain_state", "hex"),
    ("terrresponselatch", "terrain_response_latch", "hex"),
    ("scenestate", "scene_state", "hex"),
    ("scenecursor", "scene_script_cursor", "hex"),
    ("scenedata", "scene_script_data", "hex"),
    ("scenetable", "scene_table_index", "hex"),
    ("scenepending", "scene_script_pending", "hex"),
    ("scenevdp", "scene_vdp_update", "hex"),
    ("sceneclear", "scene_vdp_clear", "hex"),
    ("sceneevent", "scene_transition_event", "hex"),
    ("scenecountdown", "scene_script_countdown_value", "hex"),
    ("scenegate", "scene_script_gate", "hex"),
    ("scenegateplayer", "scene_player_gate", "hex"),
    ("scenelockplayer", "scene_player_lock", "hex"),
    ("scenecountdownplayer", "scene_player_countdown", "hex"),
    ("sceneterminalplayer", "scene_player_terminal", "hex"),
)

def _sync_v2_pattern() -> re.Pattern[str]:
    parts = [r"OPENALADDIN_SYNC"]
    for label, name, kind in SYNC_V2_FIELDS:
        value = r"\d+" if kind == "decimal" else r"[0-9A-F]+"
        parts.append(rf"{label}=(?P<{name}>{value})")
    return re.compile(r" ".join(parts))

SYNC_V2_PATTERN = _sync_v2_pattern()

SYNC_ACTOR_PATTERN = re.compile(
    r"OPENALADDIN_SYNC_ACTOR frame=(?P<frame>\d+) slot=(?P<slot>\d+) "
    r"type=(?P<type>[0-9A-F]+) x=(?P<x>[0-9A-F]+) y=(?P<y>[0-9A-F]+) "
    r"movement=(?P<movement_flags>[0-9A-F]+) facing=(?P<facing_x_flip>[0-9A-F]+) "
    r"movementpc=(?P<movement_pc>[0-9A-F]+) looppc=(?P<movement_loop_pc>[0-9A-F]+) "
    r"looptimer=(?P<movement_loop_timer>[0-9A-F]+) frameptr=(?P<frame_ptr>[0-9A-F]+) "
    r"animpc=(?P<animation_pc>[0-9A-F]+) word18=(?P<movement_word_18>[0-9A-F]+) "
    r"word1a=(?P<movement_word_1a>[0-9A-F]+) facingy=(?P<facing_y_flip>[0-9A-F]+) "
    r"movementtimer=(?P<movement_command_timer>[0-9A-F]+) "
    r"animtimer=(?P<animation_timer>[0-9A-F]+) returnpc=(?P<movement_return_pc>[0-9A-F]+) "
    r"flags=(?P<flags>[0-9A-F]+)"
)

SCHEDULER_PHASE_PATTERN = re.compile(
    r"OPENALADDIN_SCHEDULER_PHASE NAME=(?P<name>[A-Za-z0-9_.-]+) "
    r"PC=(?P<pc>[0-9A-Fa-f]+) FRAME=(?P<frame>[0-9A-Fa-f]+)"
)

SCHEDULER_WRITE_PATTERN = re.compile(
    r"OPENALADDIN_SCHEDULER_WRITE PC=(?P<pc>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+) ADDR=(?P<address>[0-9A-Fa-f]+) "
    r"DATA=(?P<data>[0-9A-Fa-f]+)"
)

def _signed_u16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value

ANIMATION_SELECTOR_FIELDS = (
    "animation_gate",
    "terminal_transition",
    "scene_script_countdown",
    "interaction_lock",
    "response_active",
    "landing_state",
    "transition_gate",
    "transition_lock",
    "transition_state",
    "transition_mode",
    "transition_flag",
    "transition_response",
    "transition_state_de",
    "transition_state_df",
    "camera_special_mode",
    "response_latch",
    "response_animation",
    "response_state_ee",
    "response_state_ef",
    "response_state_f0",
    "response_state_101",
    "horizontal_response",
    "response_timer",
    "interaction_pending",
    "state_lock",
)

def animation_selector_spec(player: dict[str, Any]) -> str | None:
    selector = player.get("animation_selector")
    if not isinstance(selector, dict):
        return None
    if any(field not in selector for field in ANIMATION_SELECTOR_FIELDS):
        return None
    return ",".join(str(int(selector[field])) for field in ANIMATION_SELECTOR_FIELDS)

def _sync_record(match: re.Match[str]) -> dict[str, int]:
    values = {
        name: int(value, 10 if name == "frame" else 16)
        for name, value in match.groupdict().items()
    }
    values["vx"] = _signed_u16(values["vx"])
    values["vy"] = _signed_u16(values["vy"])
    values["scroll_x"] = _signed_u16(values["scroll_x"])
    values["scroll_y"] = _signed_u16(values["scroll_y"])
    values["horizontal_response"] = _signed_u16(values["horizontal_response"])
    return values

def _sync_v2_record(match: re.Match[str]) -> dict[str, int]:
    values = {
        name: int(value, 10 if name == "frame" else 16)
        for name, value in match.groupdict().items()
    }
    for field in (
        "vx",
        "vy",
        "scroll_x",
        "scroll_y",
        "horizontal_response",
        "terrain_horizontal_response",
    ):
        values[field] = _signed_u16(values[field])
    return values

def _sync_actor_record(match: re.Match[str]) -> dict[str, int]:
    values = {
        name: int(value, 10 if name in {"frame", "slot"} else 16)
        for name, value in match.groupdict().items()
    }
    values["movement_word_18"] = _signed_u16(values["movement_word_18"])
    values["movement_word_1a"] = _signed_u16(values["movement_word_1a"])
    return values

def _trace_rom_bytes(header: dict[str, Any], rom_path: Path | None) -> bytes | None:
    candidates: list[Path] = []
    if rom_path is not None:
        candidates.append(rom_path)
    rom_name = str(header.get("rom", ""))
    if rom_name:
        candidates.append(ROOT / "rom" / Path(rom_name).name)
    candidates.append(default_rom())
    for candidate in candidates:
        if candidate.is_file():
            return candidate.read_bytes()
    return None

def _signed_u8(value: int) -> int:
    return value - 0x100 if value & 0x80 else value

def _collision_box_for_actor(actor: dict[str, Any], rom_bytes: bytes | None) -> dict[str, int] | None:
    if rom_bytes is None:
        return None
    frame_pointer = int(actor.get("frame_ptr", 0))
    if frame_pointer <= 0 or frame_pointer + 5 >= len(rom_bytes):
        return None
    origin_x = int(actor.get("x", 0))
    origin_y = int(actor.get("y", 0))
    if int(actor.get("facing_x_flip", 0)) != 0:
        left = origin_x - _signed_u8(rom_bytes[frame_pointer + 4])
        right = origin_x - _signed_u8(rom_bytes[frame_pointer + 2])
    else:
        left = origin_x + rom_bytes[frame_pointer + 2]
        right = origin_x + rom_bytes[frame_pointer + 4]
    return {
        "left": left,
        "top": origin_y + rom_bytes[frame_pointer + 3],
        "right": right,
        "bottom": origin_y + rom_bytes[frame_pointer + 5],
    }

def _atomic_actor_view(
    actor: dict[str, int],
    rom_bytes: bytes | None,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "slot": actor["slot"],
        "type": actor["type"],
        "x": actor["x"],
        "y": actor["y"],
        "movement_flags": actor["movement_flags"],
        "facing_x_flip": actor["facing_x_flip"],
        "frame_ptr": actor["frame_ptr"],
        "animation_pc": actor["animation_pc"],
        "movement_pc": actor["movement_pc"],
        "movement_loop_pc": actor["movement_loop_pc"],
        "movement_loop_timer": actor["movement_loop_timer"],
        "movement_word_18": actor["movement_word_18"],
        "movement_word_1a": actor["movement_word_1a"],
        "facing_y_flip": actor["facing_y_flip"],
        "movement_command_timer": actor["movement_command_timer"],
        "animation_timer": actor["animation_timer"],
        "movement_return_pc": actor["movement_return_pc"],
        "flags": actor["flags"],
        "flag_bit5": (actor["flags"] & 0x20) != 0,
    }
    result["collision_box"] = _collision_box_for_actor(result, rom_bytes)
    return result

def _atomic_camera_view(sync: dict[str, int], scene_state: int) -> dict[str, Any]:
    return {
        "x": sync["camera_x"],
        "y": sync["camera_y"],
        "reference_x": sync["reference_x"],
        "reference_y": sync["reference_y"],
        "horizontal_threshold": sync["horizontal_threshold"],
        "vertical_threshold": sync["vertical_threshold"],
        "scroll_x": sync["scroll_x"],
        "scroll_y": sync["scroll_y"],
        "pixel_x": sync["pixel_x"],
        "pixel_y": sync["pixel_y"],
        "tile_x": sync["tile_x"],
        "tile_y": sync["tile_y"],
        "level_width": sync["level_width"],
        "level_height": sync["level_height"],
        "update_delay": sync["update_delay"],
        "scroll_left_pending": sync["scroll_left_pending"],
        "scroll_right_pending": sync["scroll_right_pending"],
        "scroll_up_pending": sync["scroll_up_pending"],
        "scroll_down_pending": sync["scroll_down_pending"],
        "special_mode": sync["special_mode"],
        "state_08": scene_state == 8,
    }

def _atomic_terrain_view(sync: dict[str, int]) -> dict[str, int]:
    world_x = sync["terrain_world_x"]
    world_y = sync["terrain_world_y"]
    collision_y = world_y - 0x110
    return {
        "world_x": world_x,
        "world_y": world_y,
        "collision_probe_row": collision_y // 16,
        "collision_probe_column": world_x >> 4,
        "collision_probe_right_base_column": (world_x >> 4) + 2,
        "collision_probe_ceiling_column": (world_x >> 4) + 1,
        "collision_probe_landing_state": sync["terrain_landing_state"],
        "query_callback_a": sync["terrain_callback_a"],
        "query_callback_b": sync["terrain_callback_b"],
        "query_callback_c": sync["terrain_callback_c"],
        "query_result": sync["terrain_query_result"],
        "push_right": sync["terrain_push_right"],
        "push_left": sync["terrain_push_left"],
        "push_up": sync["terrain_push_up"],
        "push_down": sync["terrain_push_down"],
        "behavior": sync["terrain_behavior"],
        "horizontal_response": sync["terrain_horizontal_response"],
        "response_active": sync["terrain_response_active"],
        "vertical_stop": sync["terrain_vertical_stop"],
        "landing_state": sync["terrain_landing_state"],
        "surface_mode": sync["terrain_surface_mode"],
        "surface_latch": sync["terrain_surface_latch"],
        "surface_transition_flag": sync["terrain_surface_transition_flag"],
        "stop_left_motion": sync["terrain_stop_left_motion"],
        "left_inner_probe": sync["terrain_left_inner_probe"],
        "left_outer_probe": sync["terrain_left_outer_probe"],
        "stop_right_motion": sync["terrain_stop_right_motion"],
        "right_inner_probe": sync["terrain_right_inner_probe"],
        "right_outer_probe": sync["terrain_right_outer_probe"],
        "stop_upward_motion": sync["terrain_stop_upward_motion"],
        "jump_response_counter": sync["terrain_jump_response_counter"],
        "response_timer_state": sync["terrain_response_timer_state"],
        "query_state_a": sync["terrain_query_state_a"],
        "query_state_b": sync["terrain_query_state_b"],
        "state": sync["terrain_state"],
        "response_latch": sync["terrain_response_latch"],
    }

def _atomic_scene_view(sync: dict[str, int]) -> dict[str, int]:
    return {
        "state": sync["scene_state"],
        "script_cursor": sync["scene_script_cursor"],
        "script_data_cursor": sync["scene_script_data"],
        "table_index": sync["scene_table_index"],
        "script_pending": sync["scene_script_pending"],
        "vdp_update": sync["scene_vdp_update"],
        "vdp_clear": sync["scene_vdp_clear"],
        "transition_event": sync["scene_transition_event"],
        "script_countdown": sync["scene_script_countdown_value"],
        "script_gate": sync["scene_script_gate"],
        "player_gate": sync["scene_player_gate"],
        "player_lock": sync["scene_player_lock"],
        "player_countdown": sync["scene_player_countdown"],
        "player_terminal": sync["scene_player_terminal"],
    }

def _frame_ranges(frames: set[int]) -> list[list[int]]:
    """Compress a set of logical frames into inclusive ranges."""
    if not frames:
        return []
    ordered = sorted(frames)
    ranges: list[list[int]] = []
    start = previous = ordered[0]
    for frame in ordered[1:]:
        if frame != previous + 1:
            ranges.append([start, previous])
            start = frame
        previous = frame
    ranges.append([start, previous])
    return ranges

def _animation_state_records(state: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    """Return animation records exposed by the shared frame-state schema."""
    records: list[tuple[str, dict[str, Any]]] = []
    player = state.get("player")
    if isinstance(player, dict):
        records.append(("player", player))
    actors = state.get("actors")
    if isinstance(actors, list):
        for actor in actors:
            if not isinstance(actor, dict) or "slot" not in actor:
                continue
            records.append((f"actors[{actor['slot']}]", actor))
    return records

def _normalize_animation_write_order(states: dict[int, dict[str, Any]]) -> int:
    """Repair samples taken between the VM's frame-pointer and cursor writes.

    The original common animation VM writes the decoded frame pointer before
    storing the advanced animation cursor and before completing a timer
    decrement. A video/debugger boundary can therefore observe one sample
    with the new ``frame_ptr`` and an old cursor or timer. That is not a
    distinct game state: the next sample has the completed fields. Normalize
    only these strict three-sample signatures so intentional stream roots and
    duplicate frame references remain untouched.
    """
    normalized = 0
    frames = sorted(states)
    snapshot = {
        frame: json.loads(json.dumps(states[frame]))
        for frame in frames
    }
    for previous_frame, frame, next_frame in zip(frames, frames[1:], frames[2:]):
        if previous_frame + 1 != frame or frame + 1 != next_frame:
            continue
        previous = dict(_animation_state_records(snapshot[previous_frame]))
        current = dict(_animation_state_records(states[frame]))
        following = dict(_animation_state_records(snapshot[next_frame]))
        for key, record in current.items():
            before = previous.get(key)
            after = following.get(key)
            if before is None or after is None:
                continue
            if not all(
                field in record and field in before and field in after
                for field in ("animation_pc", "frame_ptr")
            ):
                continue
            pointer_boundary = (
                record["animation_pc"] != 0
                and record["animation_pc"] == before["animation_pc"]
                and record["frame_ptr"] != before["frame_ptr"]
                and after["frame_ptr"] == record["frame_ptr"]
            )
            if pointer_boundary and after["animation_pc"] != record["animation_pc"]:
                record["animation_pc"] = after["animation_pc"]
                normalized += 1
            if (
                pointer_boundary
                and all(
                    field in record and field in before and field in after
                    for field in ("animation_timer",)
                )
                and record["animation_timer"] != after["animation_timer"]
                and after["animation_timer"] == max(record["animation_timer"] - 1, 0)
            ):
                record["animation_timer"] = after["animation_timer"]
                normalized += 1
    return normalized

def normalize_animation_state_trace(path: Path, destination: Path | None = None) -> int:
    """Write a normalized state trace without modifying the observation.

    ``path`` is an observation produced by MAME.  Repairs belong in a
    separate derived artifact so a later investigation can distinguish a
    debugger/video sample from an interpretation applied by this tool.
    """
    if destination is None:
        destination = path.with_name(f"{path.stem}.semantic{path.suffix}")
    records: list[dict[str, Any]] = []
    header: dict[str, Any] | None = None
    states: dict[int, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        records.append(record)
        if record.get("type") == "header":
            header = record
        elif record.get("type") in (None, "state", "frame_state") and "frame" in record:
            states[int(record["frame"])] = record
    if header is None or not states:
        return 0

    normalized = _normalize_animation_write_order(states)
    header = dict(header)
    header["source_artifact"] = path.name
    header["transformations"] = list(header.get("transformations") or [])
    header["transformations"].append({
        "name": "animation-write-order",
        "version": 1,
        "repaired_samples": normalized,
    })
    header["animation_write_order_normalized"] = normalized > 0
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="utf-8") as output:
        output.write(json.dumps(header, separators=(",", ":")) + "\n")
        for record in records:
            if record.get("type") == "header":
                continue
            if record.get("type") in (None, "state", "frame_state") and "frame" in record:
                record = states[int(record["frame"])]
            output.write(json.dumps(record, separators=(",", ":")) + "\n")
    return normalized

def normalize_derived_state_trace(path: Path) -> int:
    """Normalize a generated replay artifact in place.

    Replay traces are already derived outputs, so unlike a MAME observation
    it is safe for callers to request the same path as the destination.
    """
    return normalize_animation_state_trace(path, path)

def synchronize_state_trace(
    trace_dir: Path,
    destination_dir: Path | None = None,
    *,
    rom_path: Path | None = None,
) -> int:
    """Derive synchronized and semantic traces from a raw MAME capture.

    MAME's frame_done callback is tied to the video device, not the game's
    update loop. It can therefore observe player/camera RAM between two
    instructions. The debugger breakpoint is placed at the start of the
    game's per-frame update and reports the completed state for the following
    trace frame. The +1 mapping below is intentional and is part of the
    openaladdin-frame-state-v2 capture contract.
    """

    state_path = trace_dir / "state.jsonl"
    destination_dir = destination_dir or trace_dir
    destination_dir.mkdir(parents=True, exist_ok=True)
    synced_path = destination_dir / "state.synced.jsonl"
    semantic_path = destination_dir / "state.jsonl"
    debug_path = trace_dir / "debug.log"
    frame_trace_path = trace_dir / "trace_boot.jsonl"
    if not state_path.is_file():
        raise SystemExit(f"{state_path}: synchronized capture has no state trace")
    if not debug_path.is_file():
        raise SystemExit(f"{debug_path}: synchronized capture has no MAME debugger log")

    records: list[dict[str, Any]] = []
    header: dict[str, Any] | None = None
    states: dict[int, dict[str, Any]] = {}
    for line_number, line in enumerate(state_path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{state_path}:{line_number}: invalid JSON: {error}") from error
        records.append(record)
        if record.get("type") == "header":
            header = record
        elif record.get("type") in (None, "state", "frame_state") and "frame" in record:
            states[int(record["frame"])] = record
    if header is None:
        raise SystemExit(f"{state_path}: synchronized capture has no state header")
    rom_bytes = _trace_rom_bytes(header, rom_path)

    # Preserve the exact video-boundary observation before creating the
    # compatibility ``state.jsonl`` semantic view in the same directory.
    if semantic_path == state_path:
        raw_copy = trace_dir / "state.raw.jsonl"
        if not raw_copy.is_file():
            shutil.copyfile(state_path, raw_copy)

    frame_metadata: dict[int, dict[str, Any]] = {}
    frame_markers: list[dict[str, Any]] = []
    if frame_trace_path.is_file():
        for line_number, line in enumerate(frame_trace_path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{frame_trace_path}:{line_number}: invalid JSON: {error}") from error
            if record.get("type") == "frame" and "frame" in record:
                frame_metadata[int(record["frame"])] = record
            elif record.get("type") == "marker":
                frame_markers.append(record)

    synchronized_records: list[dict[str, int]] = []
    synchronized_actor_records: dict[int, dict[int, dict[str, int]]] = {}
    scheduler_phases: dict[int, list[dict[str, int | str]]] = {}
    scheduler_writer_pcs: dict[int, list[int]] = {}
    for line in debug_path.read_text(encoding="utf-8").splitlines():
        phase_match = SCHEDULER_PHASE_PATTERN.search(line)
        if phase_match:
            debugger_frame = int(phase_match.group("frame"), 16)
            scheduler_phases.setdefault(debugger_frame, []).append({
                "name": phase_match.group("name"),
                "pc": int(phase_match.group("pc"), 16),
            })
            continue
        write_match = SCHEDULER_WRITE_PATTERN.search(line)
        if write_match:
            debugger_frame = int(write_match.group("frame"), 16)
            pc = int(write_match.group("pc"), 16)
            writers = scheduler_writer_pcs.setdefault(debugger_frame, [])
            if not writers or writers[-1] != pc:
                writers.append(pc)
            continue
        v2_match = SYNC_V2_PATTERN.search(line)
        if v2_match:
            synchronized_records.append(_sync_v2_record(v2_match))
            continue
        legacy_match = SYNC_PATTERN.search(line)
        if legacy_match:
            synchronized_records.append(_sync_record(legacy_match))
            continue
        actor_match = SYNC_ACTOR_PATTERN.search(line)
        if actor_match:
            actor = _sync_actor_record(actor_match)
            synchronized_actor_records.setdefault(actor["frame"], {})[actor["slot"]] = actor
    if not synchronized_records:
        raise SystemExit(f"{debug_path}: no OPENALADDIN_SYNC records found")

    # MAME's debugger ``frame`` variable is restored from a save state and is
    # therefore not always the logical frame counter maintained by the Lua
    # harness.  Fresh power-on runs line up directly; loaded-state runs are
    # aligned by the ordered raw frame stream when the absolute labels do not
    # overlap it. This keeps the state contract logical and explicit without
    # baking a checkpoint-specific frame offset into callers.
    direct = {
        parsed["frame"] + 1: parsed for parsed in synchronized_records
    }
    overlap = sum(frame in states for frame in direct)
    if overlap >= max(1, len(direct) // 2):
        synchronized = direct
        sync_mapping = "debugger frame F -> logical state S[F+1]"
    else:
        logical_frames = sorted(frame for frame in states if frame > 0)
        synchronized = {
            logical_frames[index]: parsed
            for index, parsed in enumerate(synchronized_records)
            if index < len(logical_frames)
        }
        sync_mapping = (
            "ordered debugger samples aligned to logical raw frames after "
            "save-state restore"
        )

    synchronized_actors: dict[int, dict[int, dict[str, int]]] = {}
    if synchronized_actor_records:
        if sync_mapping == "debugger frame F -> logical state S[F+1]":
            synchronized_actors = {
                frame + 1: actors
                for frame, actors in synchronized_actor_records.items()
            }
        else:
            logical_frames = sorted(frame for frame in states if frame > 0)
            actor_groups = [
                synchronized_actor_records.get(parsed["frame"], {})
                for parsed in synchronized_records
            ]
            synchronized_actors = {
                logical_frames[index]: actor_groups[index]
                for index in range(min(len(logical_frames), len(actor_groups)))
            }

    def map_debugger_groups(
        groups: dict[int, Any],
    ) -> dict[int, Any]:
        if not groups:
            return {}
        if sync_mapping == "debugger frame F -> logical state S[F+1]":
            return {frame + 1: value for frame, value in groups.items()}
        logical_frames = sorted(frame for frame in states if frame > 0)
        return {
            logical_frames[index]: groups[debugger_frame]
            for index, debugger_frame in enumerate(sorted(groups))
            if index < len(logical_frames)
        }

    synchronized_scheduler_phases = map_debugger_groups(scheduler_phases)
    synchronized_scheduler_writers = map_debugger_groups(scheduler_writer_pcs)

    all_frames = set(states)
    all_frames.update(synchronized)
    expected_actor_slots = set(range(32))
    atomic_frames: set[int] = set()
    for frame in sorted(synchronized):
        sync = synchronized[frame]
        previous = max((candidate for candidate in states if candidate < frame), default=None)
        base = states.get(frame) or states.get(previous or 0)
        if base is None:
            raise SystemExit(f"{state_path}: cannot construct synchronized frame {frame}")
        record = json.loads(json.dumps(base))
        record["type"] = "state"
        record["frame"] = frame
        record["pc"] = sync["pc"]
        record["scheduler"] = {
            "frame_phase": sync.get("frame_phase", 0),
        }

        metadata = frame_metadata.get(frame)
        if metadata is not None:
            if "input" in metadata:
                record["input"] = metadata["input"]
            if isinstance(metadata.get("scene"), dict):
                record["scene"] = metadata["scene"]
            if isinstance(metadata.get("terrain"), dict):
                record["terrain"] = metadata["terrain"]

        actor_snapshot = synchronized_actors.get(frame, {})
        actor_snapshot_complete = set(actor_snapshot) == expected_actor_slots
        if actor_snapshot_complete:
            atomic_frames.add(frame)
            atomic_actors = [
                _atomic_actor_view(actor_snapshot[slot], rom_bytes)
                for slot in range(32)
            ]
            record["actors"] = atomic_actors
            scene_state = sync["scene_state"]
            record["scene"] = _atomic_scene_view(sync)
            record["terrain"] = _atomic_terrain_view(sync)
            record["camera"] = _atomic_camera_view(sync, scene_state)

        player = record.setdefault("player", {})
        for name in (
            "x",
            "y",
            "world_x",
            "world_y",
            "vx",
            "vy",
            "frame_ptr",
            "facing_x_flip",
            "animation_pc",
            "animation_timer",
        ):
            player[name] = sync[name]
        # Lua's canonical state schema treats TERRAIN_LANDING_STATE == 1 as
        # grounded.  0xFF is the active response latch during the jump
        # transition, not the externally reported grounded boolean.
        player["grounded"] = sync["grounded"] == 1
        player["animation_selector"] = {
            name: sync[name]
            for name in ANIMATION_SELECTOR_FIELDS
        }
        if "actor_flags" in sync:
            player["actor_flags"] = sync["actor_flags"]
            player["actor_flag_bit5"] = (sync["actor_flags"] & 0x20) != 0
            player["attack_timer"] = sync["attack_timer"]
            player["attack_active"] = sync["attack_timer"] != 0
        if actor_snapshot_complete:
            player["collision_box"] = atomic_actors[0]["collision_box"]

        camera = record.setdefault("camera", {})
        for name in (
            "camera_x",
            "camera_y",
            "reference_x",
            "reference_y",
            "scroll_x",
            "scroll_y",
            "horizontal_threshold",
            "vertical_threshold",
            "update_delay",
            "special_mode",
        ):
            camera[name.removeprefix("camera_")] = sync[name]
        if not actor_snapshot_complete:
            camera["state_08"] = sync["special_mode"] != 0
        states[frame] = record

    for frame, phases in synchronized_scheduler_phases.items():
        if frame not in states:
            continue
        states[frame]["causal"] = {
            "phase_order": [phase["name"] for phase in phases],
            "phase_pcs": [phase["pc"] for phase in phases],
            "writer_pcs": synchronized_scheduler_writers.get(frame, []),
        }
    for frame, writer_pcs in synchronized_scheduler_writers.items():
        if frame not in states or frame in synchronized_scheduler_phases:
            continue
        states[frame]["causal"] = {
            "phase_order": [],
            "phase_pcs": [],
            "writer_pcs": writer_pcs,
        }

    # Mark the qualification of every emitted semantic record explicitly.
    # Raw video samples remain available in state.raw.jsonl; this flag tells
    # strict consumers whether the derived record is an atomic game-loop
    # observation or a compatibility sample with inherited fields.
    for frame, record in states.items():
        atomic = frame in atomic_frames
        record["capture"] = {
            "boundary": "game-loop" if frame in synchronized else "video-frame-done",
            "atomic": atomic,
            "atomic_fields": list(ATOMIC_STATE_FIELDS) if atomic else [],
            "atomic_actor_fields": list(ATOMIC_ACTOR_FIELDS) if atomic else [],
        }

    normalized = _normalize_animation_write_order(states)
    header = dict(header)
    header["source_artifact"] = state_path.name
    source_format = header.get("format")
    has_v2_sync = any("actor_flags" in record for record in synchronized.values())
    header["format"] = STATE_FORMAT_V2 if has_v2_sync else source_format
    if has_v2_sync:
        for record in states.values():
            record["format"] = STATE_FORMAT_V2
    header["state_boundary"] = "game-loop"
    header["sync"] = {
        "boundary": "VBlankInterrupt",
        "state_boundary": "game-loop",
        "coverage": len(synchronized) / max(len(all_frames), 1),
        "mapping": sync_mapping,
        "atomic_fields": list(ATOMIC_STATE_FIELDS) if atomic_frames else [],
        "atomic_actor_fields": list(ATOMIC_ACTOR_FIELDS) if atomic_frames else [],
        "atomic_frame_count": len(atomic_frames),
        "atomic_coverage": len(atomic_frames) / max(len(all_frames), 1),
        "atomic_frame_ranges": _frame_ranges(atomic_frames),
        "actor_slot_count": 32,
        "actors_qualified": bool(atomic_frames)
        and len(atomic_frames) == len(synchronized),
        "scheduler_trace": bool(synchronized_scheduler_phases or synchronized_scheduler_writers),
    }
    header["capture"] = {
        "boundary": "game-loop",
        "atomic_fields": list(ATOMIC_STATE_FIELDS) if atomic_frames else [],
        "atomic_actor_fields": list(ATOMIC_ACTOR_FIELDS) if atomic_frames else [],
        "atomic_frame_count": len(atomic_frames),
        "atomic_coverage": len(atomic_frames) / max(len(all_frames), 1),
        "actor_slot_count": 32,
        "actors_qualified": bool(atomic_frames)
        and len(atomic_frames) == len(synchronized),
        "scheduler_trace": bool(synchronized_scheduler_phases or synchronized_scheduler_writers),
    }
    if source_format is not None:
        header["source_format"] = source_format
    header["transformations"] = list(header.get("transformations") or [])
    header["transformations"].append({
        "name": "game-loop-sync",
        "version": 2,
        "synchronized_frames": len(synchronized),
        "atomic_frames": len(atomic_frames),
        "atomic_fields": list(ATOMIC_STATE_FIELDS) if atomic_frames else [],
        "atomic_actor_fields": list(ATOMIC_ACTOR_FIELDS) if atomic_frames else [],
    })

    # Keep the header and marker records, but emit the completed state stream
    # in frame order so downstream tools do not need to know how the debugger
    # records were merged.
    markers = [record for record in records if record.get("type") == "marker"]
    known_markers = {(record.get("frame"), record.get("name")) for record in markers}
    markers.extend(
        record for record in frame_markers
        if (record.get("frame"), record.get("name")) not in known_markers
    )
    with synced_path.open("w", encoding="utf-8") as output:
        output.write(json.dumps(header, separators=(",", ":")) + "\n")
        for frame in sorted(all_frames | set(states)):
            if frame in states:
                output.write(json.dumps(states[frame], separators=(",", ":")) + "\n")
            for marker in markers:
                if int(marker.get("frame", -1)) == frame:
                    output.write(json.dumps(marker, separators=(",", ":")) + "\n")
    normalize_animation_state_trace(synced_path, semantic_path)
    return len(synchronized)

def aligned_trace(
    source: Path,
    destination: Path,
    marker_name: str,
    fields: list[str],
) -> tuple[int, int, dict[str, Any]]:
    header, states, markers = load_state_trace(source)
    matching = [marker for marker in markers if marker.get("name") == marker_name]
    if not matching:
        known = ", ".join(str(marker.get("name")) for marker in markers) or "none"
        raise SystemExit(f"{source}: checkpoint marker {marker_name!r} not found (markers: {known})")
    checkpoint_frame = int(matching[0]["frame"])
    if checkpoint_frame not in states:
        raise SystemExit(f"{source}: checkpoint marker frame {checkpoint_frame} has no state record")

    selected = sorted(frame for frame in states if frame >= checkpoint_frame)
    if not selected or selected[0] != checkpoint_frame:
        raise SystemExit(f"{source}: no state records at or after checkpoint frame {checkpoint_frame}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    aligned_header = dict(header)
    aligned_header["frame_limit"] = len(selected) - 1
    aligned_header["alignment"] = {
        "marker": marker_name,
        "source_frame": checkpoint_frame,
        "fields": fields,
    }
    with destination.open("w", encoding="utf-8") as output:
        output.write(json.dumps(aligned_header, separators=(",", ":")) + "\n")
        for relative_frame, source_frame in enumerate(selected):
            record = json.loads(json.dumps(states[source_frame]))
            record["type"] = "state"
            record["frame"] = relative_frame
            player = record.setdefault("player", {})
            if "grounded" not in player:
                player["grounded"] = int(player.get("vy", 0)) == 0
            output.write(json.dumps(record, separators=(",", ":")) + "\n")
    return len(selected) - 1, checkpoint_frame, states[checkpoint_frame]

def compress_input_schedule(tokens: list[str]) -> str:
    if not tokens:
        return "none"
    result: list[str] = []
    start = 0
    while start < len(tokens):
        end = start + 1
        while end < len(tokens) and tokens[end] == tokens[start]:
            end += 1
        result.append(f"{tokens[start]}*{end - start}")
        start = end
    return ",".join(result)

__all__ = [name for name in globals() if not name.startswith("__")]
