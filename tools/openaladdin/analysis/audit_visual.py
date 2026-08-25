#!/usr/bin/env python3
"""Replay one MAME visual checkpoint in native and produce a diff report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from openaladdin.analysis.visual_diff import compare, parse_region, read_image, write_ppm


ROOT = Path(__file__).resolve().parents[3]


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path, required=True)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--native-binary", type=Path, default=ROOT / "build/openaladdin")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--region", help="only compare x,y,width,height")
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

    try:
        checkpoint = load_checkpoint(trace_dir, args.frame)
        command = checkpoint_command(args.native_binary.resolve(), checkpoint, native_output)
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
        region = parse_region(args.region, expected[0], expected[1])
        diff = compare(expected, actual, region)
        overlay_pixels = diff.pop("overlay_pixels")
        write_ppm(overlay_output, expected[0], expected[1], overlay_pixels)
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
        report_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(
            f"visual audit: frame={args.frame} "
            f"different={diff['different_pixels']}/{diff['compared_pixels']} "
            f"max_channel_delta={diff['max_channel_delta']} "
            f"bbox={diff['bounding_box']}"
        )
        if args.report_only:
            return 0
        if (
            diff["different_pixels"] > args.max_different_pixels
            or diff["max_channel_delta"] > args.max_channel_error
        ):
            return 1
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"visual audit: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
