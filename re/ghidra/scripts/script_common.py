"""Helpers shared by the PyGhidra headless scripts."""

import json

from ghidra.program.model.symbol import SourceType


def context(args):
    if not args:
        raise RuntimeError("OpenAladdin scripts require the generated analysis.json argument")
    with open(args[0], "r") as stream:
        return json.load(stream)


def addr(program, value):
    return program.getAddressFactory().getDefaultAddressSpace().getAddress(int(value))


def label(program, address, name):
    try:
        program.getSymbolTable().createLabel(address, name, SourceType.USER_DEFINED)
    except Exception as exc:
        print("Could not create label {} at {}: {}".format(name, address, exc))


def comment(program, address, text):
    if not text:
        return
    code_unit = program.getListing().getCodeUnitAt(address)
    if code_unit is not None:
        code_unit.setComment(code_unit.EOL_COMMENT, text)


def code_address(program, value):
    address = addr(program, value)
    block = program.getMemory().getBlock(address)
    return address if block is not None and block.isExecute() else None
