"""Export the whole-ROM static analysis database used by Genie queries.

This intentionally emits plain JSON rather than a Ghidra project format.  The
project remains disposable; the generated database is the stable boundary for
fast queries and later deassembly work.
"""

import json
import os

from ghidra.app.decompiler import DecompInterface

from script_common import context


def address_value(address):
    if address is None:
        return None
    return "0x{:08X}".format(address.getOffset() & 0xFFFFFFFF)


def function_name(function, tracked_symbols):
    address = function.getEntryPoint().getOffset()
    tracked = tracked_symbols.get(address)
    if tracked:
        return tracked
    name = function.getName()
    # Ghidra's auto-generated names are tool/version dependent.  Keep other
    # user/vector names intact, but make these names stable for downstream
    # deasm generation.
    if name.startswith("FUN_") or name.startswith("sub_"):
        return "Func_{:08X}".format(address)
    return name


def function_value(function, tracked_symbols):
    if function is None:
        return None
    entry = function.getEntryPoint()
    body = function.getBody()
    start = body.getMinAddress()
    end = body.getMaxAddress()
    return {
        "address": address_value(entry),
        "name": function_name(function, tracked_symbols),
        "start": address_value(start),
        "end": address_value(end),
        "size": int(body.getNumAddresses()),
        "thunk": bool(function.isThunk()),
    }


def function_key(function):
    return address_value(function.getEntryPoint()) if function else None


def reference_value(reference, listing, function_manager):
    from_address = reference.getFromAddress()
    to_address = reference.getToAddress()
    reference_type = reference.getReferenceType()
    instruction = listing.getInstructionAt(from_address)
    from_function = function_manager.getFunctionContaining(from_address)
    to_function = function_manager.getFunctionContaining(to_address)
    return {
        "from": address_value(from_address),
        "to": address_value(to_address),
        "from_function": function_key(from_function),
        "to_function": function_key(to_function),
        "from_function_name": from_function.getName() if from_function else None,
        "to_function_name": to_function.getName() if to_function else None,
        "type": str(reference_type),
        "read": bool(reference_type.isRead()),
        "write": bool(reference_type.isWrite()),
        "call": bool(reference_type.isCall()),
        "indirect": bool(reference_type.isIndirect()),
        "operand_index": reference.getOperandIndex(),
        "instruction": instruction.toString() if instruction else None,
    }


def all_references(program, listing, function_manager):
    """Collect memory and flow references without duplicate edges."""

    manager = program.getReferenceManager()
    records = {}
    sources = manager.getReferenceSourceIterator(program.getMinAddress(), True)
    while sources.hasNext():
        source = sources.next()
        for reference in manager.getReferencesFrom(source):
            record = reference_value(reference, listing, function_manager)
            key = (record["from"], record["to"], record["operand_index"], record["type"])
            records[key] = record

    # Flow references are not necessarily part of the memory-reference
    # iterator on every processor language, so add them from instructions too.
    for block in program.getMemory().getBlocks():
        if not block.isExecute():
            continue
        instructions = listing.getInstructions(block.getStart(), True)
        while instructions.hasNext():
            instruction = instructions.next()
            for reference in instruction.getReferencesFrom():
                record = reference_value(reference, listing, function_manager)
                key = (record["from"], record["to"], record["operand_index"], record["type"])
                records[key] = record
            for reference in manager.getFlowReferencesFrom(instruction.getAddress()):
                record = reference_value(reference, listing, function_manager)
                key = (record["from"], record["to"], record["operand_index"], record["type"])
                records[key] = record
    return [records[key] for key in sorted(records)]


def indirect_calls(program, listing, function_manager):
    """Record computed calls even when Ghidra has no resolved target edge."""

    result = []
    seen = set()
    for block in program.getMemory().getBlocks():
        if not block.isExecute():
            continue
        instructions = listing.getInstructions(block.getStart(), True)
        while instructions.hasNext():
            instruction = instructions.next()
            flow = instruction.getFlowType()
            if not flow.isComputed() or not flow.isCall():
                continue
            from_address = instruction.getAddress()
            from_function = function_manager.getFunctionContaining(from_address)
            targets = program.getReferenceManager().getFlowReferencesFrom(from_address)
            if not targets:
                targets = [None]
            for reference in targets:
                to_address = reference.getToAddress() if reference else None
                key = (address_value(from_address), address_value(to_address))
                if key in seen:
                    continue
                seen.add(key)
                result.append({
                    "from": address_value(from_address),
                    "to": address_value(to_address),
                    "from_function": function_key(from_function),
                    "from_function_name": from_function.getName() if from_function else None,
                    "type": str(flow),
                    "instruction": instruction.toString(),
                    "resolved": to_address is not None,
                })
    return sorted(result, key=lambda item: (item["from"], item["to"] or ""))


def jump_tables(program):
    """Recover decompiler-known switch tables and preserve failures per function."""

    result = []
    failures = []
    decompiler = DecompInterface()
    if not decompiler.openProgram(program):
        return result, ["could not open program in decompiler"]
    manager = program.getFunctionManager()
    iterator = manager.getFunctions(True)
    while iterator.hasNext():
        function = iterator.next()
        try:
            decompiled = decompiler.decompileFunction(function, 60, monitor)
            high_function = decompiled.getHighFunction()
            if high_function is None:
                continue
            for table in high_function.getJumpTables():
                cases = table.getCases() or []
                loads = []
                for load in table.getLoadTables() or []:
                    loads.append({
                        "address": address_value(load.getAddress()),
                        "entry_size": load.getSize(),
                        "count": load.getNum(),
                    })
                result.append({
                    "function": function_key(function),
                    "function_name": function.getName(),
                    "switch": address_value(table.getSwitchAddress()),
                    "cases": [address_value(case) for case in cases],
                    "load_tables": loads,
                })
        except Exception as exc:
            failures.append("{}: {}".format(function_key(function), exc))
    try:
        decompiler.dispose()
    except Exception:
        pass
    return result, failures


def address_classes(program, functions, tracked_symbols):
    """Emit named function/data ranges and conservative unknown ROM gaps."""

    listing = program.getListing()
    classes = []
    function_ranges = []
    for function in functions:
        function_ranges.append((function["start"], function["end"]))
        classes.append({
            "start": function["start"],
            "end": function["end"],
            "class": "CODE",
            "source": "ghidra.function",
            "name": function["name"],
        })

    data_ranges = []
    data_iterator = listing.getDefinedData(True)
    while data_iterator.hasNext():
        data = data_iterator.next()
        start = address_value(data.getAddress())
        end = address_value(data.getMaxAddress())
        data_ranges.append((start, end))
        classes.append({
            "start": start,
            "end": end,
            "class": "DATA",
            "source": "ghidra.defined_data",
            "name": data.getLabel() if data.getLabel() else None,
        })

    tracked_ranges = []
    for symbol in tracked_symbols:
        kind = str(symbol.get("kind", "")).lower()
        if kind == "function":
            continue
        start = int(str(symbol["address"]), 0)
        raw_range = symbol.get("range") or {}
        end = int(str(raw_range.get("end", symbol.get("end", start))), 0)
        if end < start:
            end = start
        tracked_ranges.append((start, end))
        classes.append({
            "start": "0x{:08X}".format(start),
            "end": "0x{:08X}".format(end),
            "class": kind.upper() if kind else "UNKNOWN",
            "source": "tracked.symbol",
            "name": symbol.get("name"),
        })

    # Memory blocks describe the address spaces outside the ROM as well.  The
    # ranges are useful even when no instruction/data was defined in them.
    for block in program.getMemory().getBlocks():
        if block.getName() == "ROM":
            continue
        block_class = "RAM" if block.isWrite() else "IO"
        classes.append({
            "start": address_value(block.getStart()),
            "end": address_value(block.getEnd()),
            "class": block_class,
            "source": "ghidra.memory_block",
            "name": block.getName(),
        })

    rom = program.getMemory().getBlock("ROM")
    if rom is not None:
        rom_start = rom.getStart().getOffset()
        rom_end = rom.getEnd().getOffset()
        covered = []
        for left, right in function_ranges + data_ranges + [
            ("0x{:08X}".format(left), "0x{:08X}".format(right))
            for left, right in tracked_ranges
        ]:
            left = max(rom_start, int(left, 16))
            right = min(rom_end, int(right, 16))
            if left <= right:
                covered.append((left, right))
        covered.sort()
        cursor = rom_start
        for left, right in covered:
            if left > cursor:
                classes.append({
                    "start": address_value(rom.getStart().add(cursor - rom_start)),
                    "end": address_value(rom.getStart().add(left - rom_start - 1)),
                    "class": "UNKNOWN",
                    "source": "ghidra.unclassified_gap",
                })
            cursor = max(cursor, right + 1)
        if cursor <= rom_end:
            classes.append({
                "start": address_value(rom.getStart().add(cursor - rom_start)),
                "end": address_value(rom.getEnd()),
                "class": "UNKNOWN",
                "source": "ghidra.unclassified_gap",
            })
    return sorted(classes, key=lambda item: (item["start"], item["end"], item["class"], item.get("name") or ""))


def write_json(path, value):
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with open(path, "w") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def run():
    config = context(getScriptArgs())
    output = config.get("full_rom_export_dir") or os.path.join(config["export_dir"], "full-rom")
    if not os.path.isdir(output):
        os.makedirs(output)

    listing = currentProgram.getListing()
    function_manager = currentProgram.getFunctionManager()
    tracked_symbols = {
        int(str(item["address"]), 0): str(item["name"])
        for item in config.get("symbols", [])
        if item.get("kind") == "function"
    }
    functions = []
    iterator = function_manager.getFunctions(True)
    while iterator.hasNext():
        functions.append(function_value(iterator.next(), tracked_symbols))
    functions.sort(key=lambda item: item["address"])

    references = all_references(currentProgram, listing, function_manager)
    call_edges = [
        {"from": item["from_function"], "to": item["to_function"], "site": item["from"], "type": item["type"]}
        for item in references
        if item["call"] and item["from_function"] and item["to_function"]
    ]
    call_edges.sort(key=lambda item: (item["from"], item["to"], item["site"]))
    reads = [item for item in references if item["read"]]
    writes = [item for item in references if item["write"]]
    indirect = indirect_calls(currentProgram, listing, function_manager)
    tables, table_failures = jump_tables(currentProgram)
    tables.sort(key=lambda item: (item["function"], item["switch"] or ""))
    classes = address_classes(currentProgram, functions, config.get("symbols", []))

    write_json(os.path.join(output, "functions.json"), functions)
    write_json(os.path.join(output, "callgraph.json"), {"format": "openaladdin-callgraph-v1", "edges": call_edges})
    write_json(os.path.join(output, "xrefs.json"), {"format": "openaladdin-xrefs-v1", "references": references})
    write_json(os.path.join(output, "memory_reads.json"), {"format": "openaladdin-memory-reads-v1", "references": reads})
    write_json(os.path.join(output, "memory_writes.json"), {"format": "openaladdin-memory-writes-v1", "references": writes})
    write_json(os.path.join(output, "indirect_calls.json"), {"format": "openaladdin-indirect-calls-v1", "references": indirect})
    write_json(os.path.join(output, "jump_tables.json"), {"format": "openaladdin-jump-tables-v1", "tables": tables, "errors": table_failures})
    write_json(os.path.join(output, "address_classes.json"), {"format": "openaladdin-address-classes-v1", "classes": classes})

    metadata = {
        "format": "openaladdin-ghidra-full-rom-v1",
        "rom": config.get("rom_identity", {}),
        "rom_size": config.get("rom_size"),
        "program": currentProgram.getDomainFile().getName(),
        "language": str(currentProgram.getLanguageID()),
        "files": [
            "metadata.json", "functions.json", "callgraph.json", "xrefs.json",
            "memory_reads.json", "memory_writes.json", "indirect_calls.json",
            "jump_tables.json", "address_classes.json",
        ],
        "counts": {
            "functions": len(functions),
            "callgraph_edges": len(call_edges),
            "xrefs": len(references),
            "memory_reads": len(reads),
            "memory_writes": len(writes),
            "indirect_calls": len(indirect),
            "jump_tables": len(tables),
            "address_classes": len(classes),
            "jump_table_errors": len(table_failures),
            "tracked_symbols": len(config.get("symbols", [])),
        },
    }
    write_json(os.path.join(output, "metadata.json"), metadata)
    print("Exported whole-ROM database: {} functions, {} references to {}".format(len(functions), len(references), output))


run()
