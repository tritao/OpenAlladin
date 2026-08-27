"""Repository tooling packages.

The package marker keeps the pre-Genie ``tools.openaladdin`` import path
available when the project is installed.  The command-line compatibility
entry point remains ``tools/oa.py`` during the migration.
"""

from __future__ import annotations

import sys

from . import openaladdin as _openaladdin

# Preserve the import name used by the pre-package scripts when they are
# imported as ``tools.openaladdin`` from an installed checkout.
sys.modules.setdefault("openaladdin", _openaladdin)
