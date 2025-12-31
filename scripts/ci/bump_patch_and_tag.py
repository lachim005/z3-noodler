#!/usr/bin/env python3
"""Compute next semver patch tag from existing vX.Y.Z tags.

- Reads tags from `git tag --list 'v*' --sort=v:refname`.
- Picks the maximum tag matching strict `vMAJOR.MINOR.PATCH`.
- Outputs the next patch tag (e.g. v1.5.1).

This script does not modify git state.
"""

from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass


_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


@dataclass(frozen=True, order=True)
class SemVer:
    major: int
    minor: int
    patch: int

    def bump_patch(self) -> "SemVer":
        return SemVer(self.major, self.minor, self.patch + 1)

    def to_tag(self) -> str:
        return f"v{self.major}.{self.minor}.{self.patch}"


def _run_git(args: list[str]) -> str:
    proc = subprocess.run(
        ["git", *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return proc.stdout


def parse_latest_semver_tag(tags_text: str) -> SemVer:
    best: SemVer | None = None
    for line in tags_text.splitlines():
        line = line.strip()
        if not line:
            continue
        match = _TAG_RE.match(line)
        if not match:
            continue
        ver = SemVer(int(match.group(1)), int(match.group(2)), int(match.group(3)))
        if best is None or ver > best:
            best = ver

    if best is None:
        raise SystemExit("No tags matching vMAJOR.MINOR.PATCH found")
    return best


def main() -> int:
    tags = _run_git(["tag", "--list", "v*", "--sort=v:refname"])
    latest = parse_latest_semver_tag(tags)
    print(latest.bump_patch().to_tag())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
