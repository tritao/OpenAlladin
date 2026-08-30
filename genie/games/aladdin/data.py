"""Aladdin-specific evidence for semantic ROM data indexing."""

from __future__ import annotations

from typing import Any, Iterable

from genie.common import parse_int
from genie.symbols import Symbol


def _address(value: Any) -> int:
    return parse_int(value)


def _hex(value: int) -> str:
    return f"0x{value:08X}"


class AladdinSemanticDataClassifier:
    """Classify Aladdin symbols and decode Aladdin VM evidence."""

    def classify_symbol(self, symbol: Symbol) -> str | None:
        metadata = {str(key): value for key, value in symbol.metadata.items()}
        type_name = str(metadata.get("type", "")).casefold()
        name = symbol.name.upper()
        if type_name in {"actor_template", "actor_template_base"}:
            return "actor-template"
        if (
            type_name in {"rom_pointer_table", "rom_pointer", "actor_spawn_phase_animation_table"}
            or "pointer_table" in type_name
        ):
            return "pointer-table"
        if type_name == "rom_table":
            return "scene-table" if name == "LEVEL_TABLE" else "level-data"
        if (
            type_name == "sound_test_entry_table"
            or type_name.startswith("scene_")
            or "scene_resource" in type_name
            or "scene_transition" in type_name
            or type_name == "scene_script"
        ):
            return "scene-table"
        if "terrain" in type_name or "collision_profile" in type_name:
            return "terrain-data"
        if "graphics" in type_name or "sprite" in type_name:
            return "graphics"
        if type_name.startswith("z80_") or name.startswith("AUDIO_"):
            return "audio-data"
        if "ANIM" in name or "ANIMATION" in name:
            return "animation"
        if "MOVE" in name or "MOVEMENT" in name:
            return "movement"
        if "ACTOR_FRAME" in name:
            return "graphics"
        if type_name == "palette_data" or "PALETTE" in name:
            return "graphics"
        if "pointer_table" in type_name or "POINTER_TABLE" in name:
            return "pointer-table"
        if type_name == "rom_table":
            return "scene-table" if name == "LEVEL_TABLE" else "level-data"
        if "table" in type_name or "TABLE" in name:
            return "rom-data"
        return None

    def decoded_references(self, obj: dict[str, Any]) -> Iterable[dict[str, Any]]:
        """Recover actor-template consumers encoded in VM F5 records."""

        value = obj.get("value")
        decoded = obj.get("decoded")
        if not isinstance(value, dict) or value.get("kind") != "actor-template":
            return ()
        if not isinstance(decoded, dict):
            return ()
        start, end = _address(value["start"]), _address(value["end"])
        result: list[dict[str, Any]] = []
        for kind, streams in decoded.items():
            if not isinstance(streams, dict):
                continue
            for entry, stream in streams.items():
                if not isinstance(stream, dict):
                    continue
                stream_name = str(stream.get("name") or f"{kind.title()}_{entry:08X}")
                if kind == "animation":
                    records = stream.get("instructions", [])
                else:
                    records = [
                        command
                        for step in stream.get("steps", [])
                        if isinstance(step, dict)
                        for command in step.get("commands", [])
                        if isinstance(command, dict)
                    ]
                for record in records if isinstance(records, list) else ():
                    if not isinstance(record, dict) or record.get("opcode") not in {"0xF5", "0x8B"}:
                        continue
                    # F5 stores its template pointer immediately after the
                    # opcode and mode byte; the remaining ten bytes are the
                    # child placement/override payload.
                    target_text = record.get("template")
                    if target_text is not None:
                        try:
                            target = _address(target_text)
                        except (TypeError, ValueError):
                            continue
                    else:
                        raw_text = record.get("raw")
                        if not isinstance(raw_text, str):
                            continue
                        try:
                            raw = bytes.fromhex(raw_text)
                        except ValueError:
                            continue
                        if len(raw) < 6:
                            continue
                        target = int.from_bytes(raw[2:6], "big")
                    if not start <= target <= end:
                        continue
                    result.append({
                        "from": record.get("address", _hex(entry)),
                        "from_function_name": stream_name,
                        "to": _hex(target),
                        "type": f"{kind.upper()}_F5_TEMPLATE",
                        "read": False,
                        "write": False,
                        "source": f"{kind}_streams",
                        "instruction": "F5 template pointer",
                    })
        return result


def semantic_providers() -> tuple[AladdinSemanticDataClassifier, ...]:
    """Return the semantic evidence providers used by the Aladdin profile."""

    return (AladdinSemanticDataClassifier(),)


__all__ = ["AladdinSemanticDataClassifier", "semantic_providers"]
