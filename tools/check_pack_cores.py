#!/usr/bin/env python3
"""Fail if any pack ships a stale copy of the shared C++ inference core."""

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "cpp" / "NamA2Core.h"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    want = sha256(CANONICAL)
    bad = 0
    for core in sorted((ROOT / "packs").glob("*/src/NamA2Core.h")):
        got = sha256(core)
        if got != want:
            print(f"STALE {core.relative_to(ROOT)} {got[:12]} != {want[:12]}")
            bad += 1
        else:
            print(f"ok    {core.relative_to(ROOT)}")
    if bad:
        print(f"\n{bad} pack core(s) differ from cpp/NamA2Core.h")
        return 1
    print("\nall pack cores match the canonical core")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
