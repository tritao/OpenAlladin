"""Define the Genesis address map after BinaryLoader imports the ROM."""

from ghidra.program.model.symbol import SourceType

from script_common import addr, comment, context, label


def run():
    config = context(getScriptArgs())
    memory = currentProgram.getMemory()
    blocks = list(memory.getBlocks())
    rom_block = memory.getBlock("ROM")
    if rom_block is None:
        initialized = [block for block in blocks if block.getStart().getOffset() == 0]
        if not initialized:
            raise RuntimeError("BinaryLoader did not create a ROM block at address 0")
        rom_block = initialized[0]
        try:
            rom_block.setName("ROM")
        except Exception:
            pass

    rom_block.setRead(True)
    rom_block.setWrite(False)
    rom_block.setExecute(True)
    comment(currentProgram, rom_block.getStart(), "Genesis cartridge ROM; {} bytes".format(config["rom_size"]))

    for definition in config["memory_map"]["blocks"]:
        name = definition["name"]
        if name == "ROM":
            continue
        start = addr(currentProgram, definition["start"])
        size = int(definition["end"]) - int(definition["start"]) + 1
        block = memory.getBlock(name)
        if block is None:
            block = memory.createUninitializedBlock(name, start, size, False)
        block.setRead(bool(definition.get("read", True)))
        block.setWrite(bool(definition.get("write", False)))
        block.setExecute(bool(definition.get("execute", False)))
        comment(currentProgram, block.getStart(), definition.get("description"))
        label(currentProgram, block.getStart(), name)

    label(currentProgram, addr(currentProgram, 0x100), "SEGA_HEADER")
    print("Genesis memory map created: {} blocks".format(len(config["memory_map"]["blocks"])))


run()
