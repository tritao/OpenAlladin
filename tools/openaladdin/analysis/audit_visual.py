#!/usr/bin/env python3
"""Replay one MAME visual checkpoint in native and produce a diff report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from openaladdin.analysis.visual_diff import (
    compare,
    compare_masked,
    parse_region,
    read_alpha,
    read_image,
    write_ppm,
)


ROOT = Path(__file__).resolve().parents[3]
# Terrain probes use WORLD_Y - 0xF0, but the VDP player frame origin is one
# 16-pixel tile above that coordinate system.
GENESIS_PLAYER_VISUAL_Y_OFFSET = 0x100


def load_checkpoint(trace_dir: Path, frame_number: int) -> dict[str, object]:
    state_path = trace_dir / "state.jsonl"
    if not state_path.exists():
        raise ValueError(f"MAME state stream not found: {state_path}")
    for line in state_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        if record.get("type") == "state" and int(record.get("frame", -1)) == frame_number:
            return record
    raise ValueError(f"MAME state stream has no frame {frame_number}: {state_path}")


def checkpoint_command(
    binary: Path,
    checkpoint: dict[str, object],
    native_output: Path,
) -> list[str]:
    player = checkpoint["player"]
    camera = checkpoint["camera"]
    actors = checkpoint.get("actors", [])
    player_actor = next((actor for actor in actors if actor.get("slot") == 0), {})
    grounded = 1 if player.get("grounded", False) else 0
    command = [
        str(binary),
        "--no-window",
        "--render-checkpoint",
        "--checkpoint-player",
        ",".join(
            str(value) for value in (
                player["x"],
                player["y"],
                player.get("vx", 0),
                player.get("vy", 0),
                grounded,
            )
        ),
        "--checkpoint-frame-ptr",
        hex(int(player["frame_ptr"])),
        "--checkpoint-animation",
        f"{int(player.get('animation_pc', 0)):#x},{int(player.get('animation_timer', 0))}",
        "--checkpoint-facing-x-flip",
        str(int(player_actor.get("facing_x_flip", 0))),
        "--checkpoint-camera",
        ",".join(
            str(value) for value in (
                camera["x"],
                camera["y"],
                camera.get("reference_x", camera["x"]),
                camera.get("reference_y", camera["y"]),
                camera.get("scroll_x", 0),
                camera.get("scroll_y", 0),
                camera.get("state_08", False) and 8 or camera.get("scene_state", 1),
            )
        ),
        "--framebuffer-out",
        str(native_output),
    ]
    return command


def _sprite_bounds(frame_manifest: Path, frame_pointer: int) -> tuple[int, int, int, int] | None:
    """Return the visible multipart-frame bounds relative to its draw origin."""
    if not frame_manifest.exists():
        return None
    try:
        document = json.loads(frame_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    for frame in document.get("frames", []):
        address = frame.get("address")
        if isinstance(address, str):
            try:
                address = int(address, 0)
            except ValueError:
                continue
        if int(address or -1) != frame_pointer:
            continue
        bounds: list[int] | None = None
        for part in frame.get("parts", []):
            offset = part.get("offset_signed") or part.get("offset_pixels")
            tile_info = part.get("tile_info", {})
            width = tile_info.get("pixel_width")
            height = tile_info.get("pixel_height")
            if not offset or not width or not height:
                continue
            part_bounds = [
                int(offset[0]),
                int(offset[1]),
                int(offset[0]) + int(width),
                int(offset[1]) + int(height),
            ]
            if bounds is None:
                bounds = part_bounds
            else:
                bounds[0] = min(bounds[0], part_bounds[0])
                bounds[1] = min(bounds[1], part_bounds[1])
                bounds[2] = max(bounds[2], part_bounds[2])
                bounds[3] = max(bounds[3], part_bounds[3])
        if bounds is not None:
            return tuple(bounds)
        return None
    return None


def _sprite_mask(
    checkpoint: dict[str, object],
    image_width: int,
    image_height: int,
    frame_manifest: Path,
) -> tuple[int, int, bytes, dict[str, object]] | None:
    """Build a framebuffer-sized mask from the captured frame pointer's PNG."""
    player = checkpoint["player"]
    camera = checkpoint["camera"]
    frame_pointer = int(player["frame_ptr"])
    if not frame_manifest.exists():
        return None
    try:
        document = json.loads(frame_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    frame_record = None
    for candidate in document.get("frames", []):
        address = candidate.get("address")
        if isinstance(address, str):
            try:
                address = int(address, 0)
            except ValueError:
                continue
        if int(address or -1) == frame_pointer:
            frame_record = candidate
            break
    if frame_record is None:
        return None

    frame_index = int(frame_record["index"])
    frame_png = frame_manifest.parent / "frames" / f"frame{frame_index:04d}.png"
    frame_width, frame_height, alpha = read_alpha(frame_png)
    parts = frame_record.get("parts", [])
    if not parts:
        return None
    min_x = min(int((part.get("offset_signed") or part.get("offset_pixels"))[0]) for part in parts)
    min_y = min(int((part.get("offset_signed") or part.get("offset_pixels"))[1]) for part in parts)
    max_x = max(
        int((part.get("offset_signed") or part.get("offset_pixels"))[0])
        + int(part["tile_info"]["pixel_width"])
        for part in parts
    )
    player_world_x = int(player.get("world_x", int(player["x"]) + int(camera["x"])))
    player_world_y = int(player.get("world_y", int(player["y"]) + int(camera["y"])))
    origin_x = player_world_x - int(camera["x"])
    origin_y = player_world_y - GENESIS_PLAYER_VISUAL_Y_OFFSET - int(camera["y"])
    player_actor = next(
        (actor for actor in checkpoint.get("actors", []) if actor.get("slot") == 0),
        {},
    )
    flip_x = bool(player_actor.get("facing_x_flip", 0))
    canvas_x = origin_x - max_x if flip_x else origin_x + min_x
    canvas_y = origin_y + min_y
    mask = bytearray(image_width * image_height)
    for y in range(frame_height):
        for x in range(frame_width):
            if alpha[y * frame_width + x] == 0:
                continue
            screen_x = canvas_x + (frame_width - 1 - x if flip_x else x)
            screen_y = canvas_y + y
            if 0 <= screen_x < image_width and 0 <= screen_y < image_height:
                mask[screen_y * image_width + screen_x] = 255
    metadata = {
        "frame_index": frame_index,
        "frame_pointer": hex(frame_pointer),
        "frame_png": str(frame_png),
        "canvas_origin": [canvas_x, canvas_y],
        "canvas_size": [frame_width, frame_height],
        "flip_x": flip_x,
        "opaque_pixels": sum(value != 0 for value in mask),
    }
    return image_width, image_height, bytes(mask), metadata


def player_region(
    checkpoint: dict[str, object],
    image_width: int,
    image_height: int,
    padding: int,
    frame_manifest: Path,
) -> tuple[tuple[int, int, int, int], dict[str, object]]:
    """Map the ROM player origin and multipart frame to a screenshot crop."""
    if padding < 0:
        raise ValueError("--player-padding must be non-negative")
    player = checkpoint["player"]
    camera = checkpoint["camera"]
    world_x = int(player.get("world_x", int(player["x"]) + int(camera["x"])))
    world_y = int(player.get("world_y", int(player["y"]) + int(camera["y"])))
    origin_x = world_x - int(camera["x"])
    origin_y = world_y - GENESIS_PLAYER_VISUAL_Y_OFFSET - int(camera["y"])
    frame_pointer = int(player["frame_ptr"])
    bounds = _sprite_bounds(frame_manifest, frame_pointer)
    if bounds is None:
        # Fallback for traces captured before frames.json recorded the frame.
        bounds = (-32, -48, 32, 24)
    raw = (
        origin_x + bounds[0] - padding,
        origin_y + bounds[1] - padding,
        origin_x + bounds[2] + padding,
        origin_y + bounds[3] + padding,
    )
    x0 = max(0, raw[0])
    y0 = max(0, raw[1])
    x1 = min(image_width, raw[2])
    y1 = min(image_height, raw[3])
    if x1 <= x0 or y1 <= y0:
        raise ValueError(f"player sprite is outside the {image_width}x{image_height} snapshot")
    region = (x0, y0, x1 - x0, y1 - y0)
    metadata = {
        "screen_origin": [origin_x, origin_y],
        "sprite_bounds": list(bounds),
        "raw_region": list(raw),
        "region": list(region),
        "padding": padding,
        "visual_y_offset": GENESIS_PLAYER_VISUAL_Y_OFFSET,
        "frame_manifest": str(frame_manifest),
    }
    return region, metadata


def crop_image(image: tuple[int, int, bytes], region: tuple[int, int, int, int]) -> tuple[int, int, bytes]:
    width, height, pixels = image
    x0, y0, crop_width, crop_height = region
    rows = []
    for y in range(y0, y0 + crop_height):
        start = (y * width + x0) * 3
        rows.append(pixels[start:start + crop_width * 3])
    return crop_width, crop_height, b"".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path, required=True)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--native-binary", type=Path, default=ROOT / "build/openaladdin")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--vdp-trace-dir",
        type=Path,
        help="use captured VRAM/CRAM/register state from this trace for rendering",
    )
    parser.add_argument(
        "--vdp-frame",
        type=int,
        help="VDP memory frame to use (defaults to visual frame - 1)",
    )
    parser.add_argument("--region", help="only compare x,y,width,height")
    parser.add_argument(
        "--player-region",
        action="store_true",
        help="derive a crop from the MAME player origin, camera, and frame manifest",
    )
    parser.add_argument(
        "--player-padding",
        type=int,
        default=4,
        help="pixels around the derived player sprite bounds (default: 4)",
    )
    parser.add_argument("--max-different-pixels", type=int, default=0)
    parser.add_argument("--max-channel-error", type=int, default=0)
    parser.add_argument(
        "--report-only",
        action="store_true",
        help="write the report but do not fail on visual differences",
    )
    args = parser.parse_args()

    trace_dir = args.trace_dir.resolve()
    output_dir = (args.output_dir or trace_dir / "native-audit" / f"frame-{args.frame:06d}").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    reference = args.reference
    if reference is None:
        candidates = sorted((trace_dir / "snapshots").glob("*.png"))
        if len(candidates) != 1:
            raise SystemExit(
                "--reference is required unless the trace has exactly one snapshot PNG"
            )
        reference = candidates[0]
    reference = reference.resolve()
    native_output = output_dir / "native.ppm"
    overlay_output = output_dir / "diff.ppm"
    report_output = output_dir / "report.json"
    frame_manifest = ROOT / "build/assets/sprites/frames.json"

    try:
        checkpoint = load_checkpoint(trace_dir, args.frame)
        command = checkpoint_command(args.native_binary.resolve(), checkpoint, native_output)
        if args.vdp_trace_dir is not None:
            vdp_frame = args.vdp_frame if args.vdp_frame is not None else max(args.frame - 1, 0)
            command.extend([
                "--checkpoint-vdp",
                str(args.vdp_trace_dir.resolve()),
                str(vdp_frame),
            ])
        result = subprocess.run(
            command,
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            return result.returncode

        expected = read_image(reference)
        actual = read_image(native_output)
        if args.region and args.player_region:
            raise ValueError("--region and --player-region are mutually exclusive")
        player_metadata = None
        if args.player_region:
            region, player_metadata = player_region(
                checkpoint,
                expected[0],
                expected[1],
                args.player_padding,
                frame_manifest,
            )
        else:
            region = parse_region(args.region, expected[0], expected[1])
        diff = compare(expected, actual, region)
        overlay_pixels = diff.pop("overlay_pixels")
        write_ppm(overlay_output, expected[0], expected[1], overlay_pixels)
        player_diff = None
        if player_metadata is not None:
            expected_crop = crop_image(expected, region)
            actual_crop = crop_image(actual, region)
            player_overlay_output = output_dir / "player-diff.ppm"
            player_reference_output = output_dir / "player-reference.ppm"
            player_native_output = output_dir / "player-native.ppm"
            player_report = compare(expected_crop, actual_crop, (0, 0, region[2], region[3]))
            player_overlay_pixels = player_report.pop("overlay_pixels")
            write_ppm(player_reference_output, *expected_crop)
            write_ppm(player_native_output, *actual_crop)
            write_ppm(player_overlay_output, region[2], region[3], player_overlay_pixels)
            player_diff = {
                "region": player_metadata,
                "reference": str(player_reference_output),
                "native": str(player_native_output),
                "overlay": str(player_overlay_output),
                "diff": player_report,
            }
            sprite_mask = _sprite_mask(
                checkpoint,
                expected[0],
                expected[1],
                frame_manifest,
            )
            if sprite_mask is not None:
                sprite_report = compare_masked(expected, actual, sprite_mask[:3])
                sprite_overlay_pixels = sprite_report.pop("overlay_pixels")
                sprite_overlay_crop = crop_image(
                    (expected[0], expected[1], sprite_overlay_pixels),
                    region,
                )
                sprite_overlay_output = output_dir / "player-sprite-diff.ppm"
                write_ppm(sprite_overlay_output, *sprite_overlay_crop)
                player_diff["sprite"] = {
                    "mask": sprite_mask[3],
                    "overlay": str(sprite_overlay_output),
                    "diff": sprite_report,
                }
        report = {
            "format": "openaladdin-visual-audit-v1",
            "trace_dir": str(trace_dir),
            "frame": args.frame,
            "reference": str(reference),
            "native": str(native_output),
            "overlay": str(overlay_output),
            "command": command,
            "checkpoint": checkpoint,
            "diff": diff,
        }
        if player_metadata is not None:
            report["player"] = player_diff
        report_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(
            f"visual audit: frame={args.frame} "
            f"different={diff['different_pixels']}/{diff['compared_pixels']} "
            f"max_channel_delta={diff['max_channel_delta']} "
            f"bbox={diff['bounding_box']}"
        )
        if player_diff is not None:
            focused = player_diff["diff"]
            print(
                "player audit: "
                f"region={player_metadata['region']} "
                f"different={focused['different_pixels']}/{focused['compared_pixels']} "
                f"bbox={focused['bounding_box']}"
            )
            sprite = player_diff.get("sprite")
            if sprite is not None:
                focused_sprite = sprite["diff"]
                print(
                    "player sprite audit: "
                    f"frame={sprite['mask']['frame_index']} "
                    f"different={focused_sprite['different_pixels']}/"
                    f"{focused_sprite['compared_pixels']} "
                    f"bbox={focused_sprite['bounding_box']}"
                )
        if args.report_only:
            return 0
        validation_diff = diff
        if player_diff is not None and player_diff.get("sprite") is not None:
            validation_diff = player_diff["sprite"]["diff"]
        if (
            validation_diff["different_pixels"] > args.max_different_pixels
            or validation_diff["max_channel_delta"] > args.max_channel_error
        ):
            return 1
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"visual audit: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
