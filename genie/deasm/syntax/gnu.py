"""GNU assembler syntax for the Motorola 68000."""

from __future__ import annotations

import re
from typing import Mapping

from genie.common import parse_int

from ..model import InstructionRecord


_REGISTER = re.compile(r"^(?P<register>[DA][0-7]|PC|SP|SR|USP)(?P<size>[bwl])?$", re.IGNORECASE)
_ABSOLUTE = re.compile(
    r"^\(\s*(?P<value>-?(?:0x[0-9a-f]+|[0-9]+))\s*\)\.(?P<size>[bwl])$",
    re.IGNORECASE,
)
_DIRECT_TARGET = re.compile(
    r"^(?P<value>-?(?:0x[0-9a-f]+|[0-9]+))(?P<suffix>\.[bwl])?$",
    re.IGNORECASE,
)


def _split_operands(value: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    braces = 0
    for index, char in enumerate(str(value)):
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)
        elif char == "{":
            braces += 1
        elif char == "}":
            braces = max(0, braces - 1)
        elif char == "," and depth == 0 and braces == 0:
            result.append(str(value)[start:index].strip())
            start = index + 1
    tail = str(value)[start:].strip()
    if tail:
        result.append(tail)
    return result


def _register(value: str) -> str:
    match = _REGISTER.fullmatch(value.strip())
    if not match:
        return value.strip()
    return f"%{match.group('register').lower()}"


def _index_register(value: str) -> str:
    value = value.strip()
    match = _REGISTER.fullmatch(value)
    register_name = match.group("register").upper() if match else ""
    if not match or register_name[0:1] not in {"D", "A"}:
        return _register(value)
    register = f"%{match.group('register').lower()}"
    size = match.group("size")
    return f"{register}.{size.lower()}" if size else register


def _addressing(value: str) -> str:
    value = value.strip()
    absolute = _ABSOLUTE.fullmatch(value)
    if absolute:
        return f"{absolute.group('value')}.{absolute.group('size').lower()}"

    if value.startswith("-(") and value.endswith(")"):
        return f"-({_register(value[2:-1])})"
    if value.startswith("(") and value.endswith(")+"):
        return f"({_register(value[1:-2])})+"
    if value.startswith("(") and value.endswith(")"):
        inner = value[1:-1].strip()
        parts = [part.strip() for part in inner.split(",")]
        if len(parts) == 1 and _REGISTER.fullmatch(parts[0]):
            return f"({_register(parts[0])})"
        if len(parts) in (2, 3) and _REGISTER.fullmatch(parts[1]):
            displacement = parts[0]
            base = _register(parts[1])
            if len(parts) == 2:
                return f"{displacement}({base})"
            index = parts[2].strip()
            scale = ""
            if "*" in index:
                index, scale = (part.strip() for part in index.split("*", 1))
                if scale.lower() not in {"0x1", "1"}:
                    raise ValueError(f"unsupported 68000 index scale {scale!r}")
            return f"{displacement}({base},{_index_register(index)})"
        if len(parts) == 2 and parts[1].upper() == "PC":
            return f"{parts[0]}(%pc)"
    return _register(value)


def _is_immediate(value: str) -> bool:
    return value.lstrip().startswith("#")


def _target_reference(record: InstructionRecord, operand_index: int) -> int | None:
    for reference in record.references:
        try:
            if int(reference.get("operand_index", -1)) != operand_index:
                continue
            return parse_int(reference["to"])
        except (KeyError, TypeError, ValueError):
            continue
    return None


def _replace_target(
    value: str,
    target: int | None,
    labels: Mapping[int, str],
    *,
    direct: bool = False,
) -> str:
    if target is None or target not in labels:
        return value
    absolute = _ABSOLUTE.fullmatch(value.strip())
    if absolute:
        return f"{labels[target]}.{absolute.group('size').lower()}"
    if direct:
        direct_target = _DIRECT_TARGET.fullmatch(value.strip())
        if direct_target:
            suffix = direct_target.group("suffix") or ""
            return labels[target] + suffix.lower()
    return value


def _normalize_register_list(value: str) -> str:
    if not (value.startswith("{") and value.endswith("}")):
        return value
    registers = re.findall(r"[DA][0-7](?:[bwl])?", value, re.IGNORECASE)
    return "/".join(_register(register) for register in registers)


class Gnu68000Syntax:
    """Normalize Ghidra's Motorola-like operands to GNU ``as`` syntax."""

    def render_instruction(
        self,
        record: InstructionRecord,
        labels: Mapping[int, str],
    ) -> str:
        mnemonic = record.mnemonic.strip().lower()
        operands = _split_operands(record.operands)
        rendered: list[str] = []
        branch_or_call = mnemonic.startswith("b") or mnemonic in {"dbf", "dbr", "dbra", "jmp", "jsr"}
        for index, operand in enumerate(operands):
            operand = _normalize_register_list(operand.strip())
            target = _target_reference(record, index)
            if not _is_immediate(operand):
                operand = _replace_target(operand, target, labels, direct=branch_or_call)
            if operand.startswith("{"):
                normalized = _normalize_register_list(operand)
            elif operand.startswith("#"):
                normalized = "#" + _addressing(operand[1:])
            else:
                normalized = _addressing(operand)
            if normalized.startswith("#") and mnemonic.endswith(".b"):
                try:
                    immediate = parse_int(normalized[1:])
                except (TypeError, ValueError):
                    immediate = None
                if immediate is not None and immediate < 0:
                    # GAS sign-extends a negative byte literal into the
                    # immediate extension word. The original 68000 encoding
                    # commonly carries the byte as 00xx instead.
                    normalized = f"#0x{immediate & 0xFF:02X}"
            if mnemonic.startswith("link") and index == 1 and not normalized.startswith("#"):
                normalized = "#" + normalized
            # The 68000 GNU syntax requires '%' prefixes on registers, while
            # Ghidra includes an operand-size suffix on register displays.
            if normalized.startswith("{"):
                normalized = _normalize_register_list(normalized)
            rendered.append(normalized)

        # Ghidra displays the implicit immediate in these mnemonics without
        # '#'.  This is a syntax distinction, not an instruction change.
        if mnemonic in {"addq.b", "addq.w", "addq.l", "subq.b", "subq.w", "subq.l", "moveq"} and rendered:
            if not rendered[0].startswith("#"):
                rendered[0] = "#" + rendered[0]
        return f"    {mnemonic}{(' ' + ', '.join(rendered)) if rendered else ''}"

    def render_bytes(self, data: bytes) -> list[str]:
        result: list[str] = []
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            result.append("    dc.b    " + ",".join(f"0x{value:02X}" for value in chunk))
        return result

    def render_longs(self, data: bytes) -> list[str]:
        result: list[str] = []
        for offset in range(0, len(data), 4):
            value = int.from_bytes(data[offset:offset + 4], byteorder="big")
            result.append(f"    dc.l    0x{value:08X}")
        return result


__all__ = ["Gnu68000Syntax"]
