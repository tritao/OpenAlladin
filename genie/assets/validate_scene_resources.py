#!/usr/bin/env python3
"""Validate the tracked scene/resource map against static RNC loader calls."""

from __future__ import annotations

import argparse
from pathlib import Path

from genie.common import ROOT
from genie.assets.scene_resources import validate_scene_resources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--metadata",
        type=Path,
        default=ROOT / "re/assets/scene_resources.yml",
    )
    parser.add_argument(
        "--loader-analysis",
        type=Path,
        default=ROOT / "build/assets/rnc/loader_analysis.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/assets/rnc/scene_resources.json",
    )
    args = parser.parse_args()
    for path in (args.metadata, args.loader_analysis):
        if not path.is_file():
            raise SystemExit(f"required input not found: {path}")
    report = validate_scene_resources(
        args.metadata.resolve(),
        args.loader_analysis.resolve(),
        args.output.resolve(),
    )
    print(f"scene states: {report['summary']['state_count']}")
    print(f"scene resources: {report['summary']['resource_count']}")
    print(f"static matches: {report['summary']['matched_resource_count']}")
    if report["errors"]:
        for error in report["errors"]:
            print(f"ERROR {error['state']}: {error['error']}")
        return 1
    print(f"scene resource report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
