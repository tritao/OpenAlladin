from __future__ import annotations

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _imports(path: Path) -> list[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    result: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            result.extend(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            result.append(node.module)
    return result


def test_shared_layers_do_not_import_game_packages():
    roots = [ROOT / "genie/data.py", ROOT / "genie/core", ROOT / "genie/platforms"]
    violations = []
    for root in roots:
        paths = [root] if root.is_file() else list(root.rglob("*.py")) if root.is_dir() else []
        for path in paths:
            for imported in _imports(path):
                if imported == "genie.games" or imported.startswith("genie.games."):
                    violations.append(f"{path.relative_to(ROOT)} imports {imported}")
    assert violations == []
