"""Import sampled runtime PCs as repeatable Ghidra bookmarks."""

import json
import os

from ghidra.program.model.listing import BookmarkType


CATEGORY = "OpenAladdin Runtime Coverage"


def address_value(address):
    return "0x{:06X}".format(address.getOffset())


def clear_old_bookmarks(bookmark_manager):
    iterator = bookmark_manager.getBookmarksIterator(BookmarkType.NOTE)
    stale = []
    while iterator.hasNext():
        bookmark = iterator.next()
        if bookmark.getCategory() == CATEGORY:
            stale.append(bookmark)
    for bookmark in stale:
        bookmark_manager.removeBookmark(bookmark)


def run():
    arguments = getScriptArgs()
    if len(arguments) < 2:
        raise RuntimeError("usage: ImportRuntimeCoverage.py coverage.json output.json")
    coverage_path = arguments[0]
    output_path = arguments[1]
    with open(coverage_path, "r") as stream:
        coverage = json.load(stream)
    if coverage.get("format") != "openaladdin-runtime-coverage-v1":
        raise RuntimeError("unsupported runtime coverage format")

    bookmark_manager = currentProgram.getBookmarkManager()
    clear_old_bookmarks(bookmark_manager)
    function_manager = currentProgram.getFunctionManager()
    rows = []
    skipped = []
    functions = {}
    for raw_address, observation in sorted(coverage.get("pcs", {}).items()):
        value = int(raw_address, 0)
        address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value)
        block = currentProgram.getMemory().getBlock(address)
        if block is None or not block.isExecute():
            skipped.append(raw_address)
            continue
        scenarios = sorted(observation.get("scenarios", []))
        comment = "scenarios={} samples={} frames={}-{}".format(
            ",".join(scenarios),
            observation.get("sample_count", 0),
            observation.get("first_frame", 0),
            observation.get("last_frame", 0),
        )
        bookmark_manager.setBookmark(address, BookmarkType.NOTE, CATEGORY, comment)
        function = function_manager.getFunctionContaining(address)
        function_key = address_value(function.getEntryPoint()) if function is not None else None
        if function_key is not None:
            functions.setdefault(function_key, {
                "name": function.getName(),
                "scenarios": set(),
                "pc_count": 0,
            })
            functions[function_key]["scenarios"].update(scenarios)
            functions[function_key]["pc_count"] += 1
        rows.append({"address": raw_address, "function": function_key, "scenarios": scenarios})

    report = {
        "format": "openaladdin-ghidra-runtime-coverage-v1",
        "coverage": os.path.abspath(coverage_path),
        "bookmark_category": CATEGORY,
        "bookmark_count": len(rows),
        "skipped": skipped,
        "functions": {
            key: {
                "name": value["name"],
                "scenarios": sorted(value["scenarios"]),
                "pc_count": value["pc_count"],
            }
            for key, value in sorted(functions.items())
        },
        "pcs": rows,
    }
    with open(output_path, "w") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print("Imported {} runtime PC bookmark(s); skipped {} non-executable address(es)".format(
        len(rows), len(skipped)
    ))


run()
