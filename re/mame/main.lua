-- Stable MAME entrypoint. The compatibility trace_boot name remains usable
-- for existing scripts while the probe implementation lives behind modules.

local root = os.getenv("OPENALADDIN_ROOT") or "."
dofile(root .. "/re/mame/trace_boot.lua")
