-- Minimal MAME bootstrap for experiments.
-- Usage from a MAME Lua script: local symbols = dofile("re/mame/lua/symbols.lua")
local root = os.getenv("OPENALADDIN_ROOT") or "."
local symbols = dofile(root .. "/re/mame/lua/symbols.lua")
return symbols
