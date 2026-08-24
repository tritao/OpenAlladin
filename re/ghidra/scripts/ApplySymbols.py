"""Apply the tracked YAML-derived symbol database."""

from ghidra.program.model.symbol import SourceType

from script_common import addr, comment, context


def run():
    config = context(getScriptArgs())
    function_manager = currentProgram.getFunctionManager()
    applied = 0
    for symbol in config.get("symbols", []):
        address = addr(currentProgram, symbol["address"])
        name = symbol["name"]
        try:
            if symbol["kind"] == "function":
                function = function_manager.getFunctionAt(address)
                if function is None:
                    disassemble(address)
                    function = createFunction(address, name)
                elif function.getName() != name:
                    function.setName(name, SourceType.USER_DEFINED)
            else:
                createLabel(address, name, True)
            evidence = symbol.get("evidence", [])
            details = "confidence={}".format(symbol.get("confidence", "unspecified"))
            if evidence:
                details += "; evidence=" + ", ".join(str(item) for item in evidence)
            if symbol.get("description"):
                details += "; " + str(symbol["description"])
            comment(currentProgram, address, details)
            applied += 1
        except Exception as exc:
            print("Could not apply {} at {}: {}".format(name, address, exc))
    print("Applied {} tracked symbols".format(applied))


run()
