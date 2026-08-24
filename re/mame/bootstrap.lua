-- Minimal MAME bootstrap for experiments.
-- Usage from a MAME Lua script: local symbols = dofile("re/mame/symbols.lua")
local root = os.getenv("OPENALADDIN_ROOT") or "."
local symbols = dofile(root .. "/re/mame/symbols.lua")
return symbols
