"""Export Ghidra functions, blocks, and references to build/re."""

import csv
import json
import os

from script_common import context


def address_value(address):
    return "0x{:08X}".format(address.getOffset() & 0xFFFFFFFF)


def run():
    config = context(getScriptArgs())
    output = config["export_dir"]
    if not os.path.isdir(output):
        os.makedirs(output)
    functions = []
    references = []
    manager = currentProgram.getFunctionManager()
    iterator = manager.getFunctions(True)
    while iterator.hasNext():
        function = iterator.next()
        entry = function.getEntryPoint()
        callers = []
        for reference in currentProgram.getReferenceManager().getReferencesTo(entry):
            caller = address_value(reference.getFromAddress())
            callers.append(caller)
            references.append({"from": caller, "to": address_value(entry), "type": str(reference.getReferenceType())})
        functions.append({
            "address": address_value(entry),
            "name": function.getName(),
            "callers": sorted(set(callers)),
            "callees": [],
        })

    with open(os.path.join(output, "functions.json"), "w") as stream:
        json.dump(functions, stream, indent=2, sort_keys=True)
        stream.write("\n")
    with open(os.path.join(output, "functions.csv"), "w") as stream:
        writer = csv.DictWriter(stream, fieldnames=["address", "name", "callers", "callees"])
        writer.writeheader()
        for function in functions:
            writer.writerow({**function, "callers": ";".join(function["callers"]), "callees": ";".join(function["callees"])})
    with open(os.path.join(output, "references.json"), "w") as stream:
        json.dump(references, stream, indent=2, sort_keys=True)
        stream.write("\n")

    blocks = []
    for block in currentProgram.getMemory().getBlocks():
        blocks.append({
            "name": block.getName(),
            "start": address_value(block.getStart()),
            "end": address_value(block.getEnd()),
            "read": block.isRead(),
            "write": block.isWrite(),
            "execute": block.isExecute(),
        })
    with open(os.path.join(output, "memory_blocks.json"), "w") as stream:
        json.dump(blocks, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("Exported {} functions and {} references to {}".format(len(functions), len(references), output))


run()
