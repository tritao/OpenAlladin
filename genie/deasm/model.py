"""Small immutable models used by the deterministic ROM emitter."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from genie.common import parse_int
from genie.layout.model import Layout


def _operands(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        return ", ".join(str(item) for item in value)
    return str(value)


@dataclass(frozen=True, slots=True)
class InstructionRecord:
    """One canonical instruction record exported by Ghidra."""

    address: int
    size: int
    encoding: bytes
    mnemonic: str
    operands: str = ""
    function: int | None = None
    function_name: str | None = None
    references: tuple[dict[str, Any], ...] = ()

    @property
    def end(self) -> int:
        return self.address + self.size - 1

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "InstructionRecord":
        raw_encoding = str(value.get("bytes", "")).replace(" ", "").replace("_", "")
        try:
            encoding = bytes.fromhex(raw_encoding)
        except ValueError as error:
            raise ValueError(
                f"instruction at {value.get('address')!r} has invalid bytes {value.get('bytes')!r}"
            ) from error
        references = value.get("references", ()) or ()
        if not isinstance(references, (list, tuple)):
            raise ValueError(f"instruction references must be a list at {value.get('address')!r}")
        return cls(
            address=parse_int(value["address"]),
            size=parse_int(value["size"]),
            encoding=encoding,
            mnemonic=str(value.get("mnemonic", "")),
            operands=_operands(value.get("operands")),
            function=parse_int(value["function"]) if value.get("function") is not None else None,
            function_name=(str(value["function_name"]) if value.get("function_name") is not None else None),
            references=tuple(dict(item) for item in references),
        )


@dataclass(frozen=True, slots=True)
class DeasmInput:
    """Validated inputs shared by build and stats commands."""

    rom: bytes
    layout: Layout
    instructions: tuple[InstructionRecord, ...]


__all__ = ["DeasmInput", "InstructionRecord"]
