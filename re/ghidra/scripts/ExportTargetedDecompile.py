"""Decompile a focused set of loader functions and export their call context."""

import json
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor


def address_value(address):
    return "0x{:06X}".format(address.getOffset())


def reference_rows(address):
    rows = []
    references = currentProgram.getReferenceManager().getReferencesTo(address)
    while references.hasNext():
        reference = references.next()
        rows.append({
            "address": address_value(reference.getFromAddress()),
            "type": str(reference.getReferenceType()),
            "primary": reference.isPrimary(),
        })
    return sorted(rows, key=lambda row: row["address"])


def function_row(function, target):
    entry = function.getEntryPoint()
    body = function.getBody()
    result = {
        "address": address_value(entry),
        "name": function.getName(),
        "requested_targets": [target],
        "body_start": address_value(body.getMinAddress()),
        "body_end": address_value(body.getMaxAddress()),
        "callers": reference_rows(entry),
    }
    return result


def decompile_row(function, target, decompiler, monitor):
    row = function_row(function, target)
    result = decompiler.decompileFunction(function, 60, monitor)
    if result.decompileCompleted() and result.getDecompiledFunction() is not None:
        row["status"] = "decompiled"
        row["c"] = result.getDecompiledFunction().getC()
    else:
        row["status"] = "decompile_failed"
        row["error"] = result.getErrorMessage()
    return row


def memory_reference_row(target, function_manager, reference_manager, decompiler, monitor, by_function):
    address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(
        int(target["address"], 0)
    )
    references = []
    functions = []
    iterator = reference_manager.getReferencesTo(address)
    while iterator.hasNext():
        reference = iterator.next()
        from_address = reference.getFromAddress()
        function = function_manager.getFunctionContaining(from_address)
        function_key = address_value(function.getEntryPoint()) if function is not None else None
        references.append({
            "address": address_value(from_address),
            "type": str(reference.getReferenceType()),
            "primary": reference.isPrimary(),
            "function": function_key,
            "function_name": function.getName() if function is not None else None,
        })
        if function is not None:
            if function_key not in by_function:
                by_function[function_key] = decompile_row(
                    function,
                    target,
                    decompiler,
                    monitor,
                )
            functions.append(function_key)
    return {
        "address": target["address"],
        "name": target.get("name"),
        "kind": "memory_reference",
        "references": sorted(references, key=lambda row: row["address"]),
        "functions": [by_function[key] for key in sorted(set(functions))],
    }


def run():
    arguments = getScriptArgs()
    if len(arguments) < 2:
        raise RuntimeError("usage: ExportTargetedDecompile.py request.json output.json")
    request_path = arguments[0]
    output_path = arguments[1]
    with open(request_path, "r") as stream:
        request = json.load(stream)

    decompiler = DecompInterface()
    decompiler.toggleCCode(True)
    decompiler.toggleSyntaxTree(False)
    decompiler.setSimplificationStyle("decompile")
    if not decompiler.openProgram(currentProgram):
        raise RuntimeError("could not open program in decompiler")
    monitor = ConsoleTaskMonitor()
    function_manager = currentProgram.getFunctionManager()
    reference_manager = currentProgram.getReferenceManager()
    targets = []
    by_function = {}
    for target in request.get("targets", []):
        address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(
            int(target["address"], 0)
        )
        function = function_manager.getFunctionContaining(address)
        if function is None:
            targets.append({
                "address": target["address"],
                "target": target,
                "status": "no_function",
            })
            continue
        function_key = address_value(function.getEntryPoint())
        existing = by_function.get(function_key)
        if existing is not None:
            existing["requested_targets"].append(target)
            continue
        row = decompile_row(function, target, decompiler, monitor)
        by_function[function_key] = row
        targets.append(row)

    for target in request.get("memory_references", []):
        targets.append(memory_reference_row(
            target,
            function_manager,
            reference_manager,
            decompiler,
            monitor,
            by_function,
        ))

    report = {
        "format": "openaladdin-targeted-decompile-v1",
        "program": currentProgram.getName(),
        "rom": request.get("rom"),
        "focus": request.get("focus"),
        "targets": targets,
    }
    parent = os.path.dirname(output_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with open(output_path, "w") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("Exported {} targeted decompilations to {}".format(len(targets), output_path))


run()
