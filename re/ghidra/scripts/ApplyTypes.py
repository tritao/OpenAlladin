"""Create tracked structure definitions without guessing unknown fields."""

from ghidra.program.model.data import (
    ByteDataType,
    DWordDataType,
    IntegerDataType,
    ShortDataType,
    StructureDataType,
    Undefined1DataType,
)
from ghidra.program.model.data import DataTypeConflictHandler

from script_common import context


def data_type(type_name):
    if type_name in ("u8", "s8", "byte"):
        return ByteDataType()
    if type_name in ("u16", "s16", "short"):
        return ShortDataType()
    if type_name in ("u32", "s32", "int", "long"):
        return IntegerDataType()
    if type_name in ("u32be", "dword"):
        return DWordDataType()
    return Undefined1DataType()


def run():
    config = context(getScriptArgs())
    manager = currentProgram.getDataTypeManager()
    applied = 0
    for definition in config.get("types", []):
        name = definition.get("name")
        if not name:
            continue
        structure = StructureDataType(name, int(definition.get("size", 0)))
        for raw_offset, field in (definition.get("fields") or {}).items():
            offset = int(str(raw_offset), 0)
            field_type = data_type(str(field.get("type", "u8")))
            try:
                structure.replaceAtOffset(offset, field_type.getLength(), field_type, field.get("name"), field.get("description", ""))
            except Exception as exc:
                print("Could not add {}.{}: {}".format(name, field.get("name", "field"), exc))
        manager.addDataType(structure, DataTypeConflictHandler.REPLACE_HANDLER)
        applied += 1
    print("Applied {} tracked type definitions".format(applied))


run()
