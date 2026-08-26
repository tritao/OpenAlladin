"""Export code/data references to selected RAM addresses."""

import json
import os

from script_common import addr, context


def address_value(address):
    return "0x{:08X}".format(address.getOffset())


def run():
    config = context(getScriptArgs())
    output = config["output"]
    records = []
    reference_manager = currentProgram.getReferenceManager()
    listing = currentProgram.getListing()
    function_manager = currentProgram.getFunctionManager()

    for target in config["targets"]:
        target_address = addr(currentProgram, int(str(target["address"]), 0))
        for reference in reference_manager.getReferencesTo(target_address):
            from_address = reference.getFromAddress()
            instruction = listing.getInstructionAt(from_address)
            function = function_manager.getFunctionContaining(from_address)
            reference_type = reference.getReferenceType()
            records.append({
                "target": address_value(target_address),
                "target_name": target.get("name"),
                "from": address_value(from_address),
                "reference_type": str(reference_type),
                "read": reference_type.isRead(),
                "write": reference_type.isWrite(),
                "operand_index": reference.getOperandIndex(),
                "instruction": instruction.toString() if instruction else None,
                "function": address_value(function.getEntryPoint()) if function else None,
                "function_name": function.getName() if function else None,
            })

    records.sort(key=lambda row: (row["target"], row["from"]))
    parent = os.path.dirname(output)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with open(output, "w") as stream:
        json.dump({
            "format": "openaladdin-data-references-v1",
            "rom": currentProgram.getDomainFile().getName(),
            "targets": config["targets"],
            "references": records,
        }, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("Exported {} data references to {}".format(len(records), output))


run()
