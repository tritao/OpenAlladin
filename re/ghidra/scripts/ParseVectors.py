"""Parse the 68000 vector table and seed controlled disassembly."""

from script_common import addr, code_address, comment, context, label


def read_u32(address):
    return currentProgram.getMemory().getInt(address) & 0xFFFFFFFF


def run():
    config = context(getScriptArgs())
    function_manager = currentProgram.getFunctionManager()
    for vector in config["vectors"]:
        index = int(vector["index"])
        name = vector["name"]
        vector_address = addr(currentProgram, index * 4)
        target = read_u32(vector_address)
        label(currentProgram, vector_address, "Vector_{:02d}_{}".format(index, name))
        if index == 0:
            comment(currentProgram, vector_address, "Initial supervisor stack pointer: 0x{:08X}".format(target))
            continue
        if target == 0:
            continue
        target_address = code_address(currentProgram, target)
        if target_address is None:
            comment(currentProgram, vector_address, "{} target 0x{:08X} is outside executable ROM".format(name, target))
            continue
        label(currentProgram, target_address, name)
        comment(currentProgram, vector_address, "{} -> 0x{:08X}".format(name, target))
        try:
            disassemble(target_address)
            if function_manager.getFunctionAt(target_address) is None:
                createFunction(target_address, name)
        except Exception as exc:
            print("Could not seed {} at 0x{:06X}: {}".format(name, target, exc))
    print("Parsed {} vector entries".format(len(config["vectors"])))


run()
