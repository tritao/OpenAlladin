-- Load the generated symbol table inside MAME's Lua environment.
-- Keep addresses in re/symbols/*.yml; `oa ghidra rebuild` regenerates this file.
local root = os.getenv("OPENALADDIN_ROOT") or "."
return dofile(root .. "/build/re/mame_symbols.lua")
