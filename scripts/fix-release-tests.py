#!/usr/bin/env python3
"""Drop the two ReleaseCatalog tests for ReleaseInfo::pickAsset.

Production picks an asset through RuntimeInstaller::pickAsset, which matches
the backend differently; ReleaseInfo::pickAsset was a second, parallel picker
that only these tests exercised.
"""
import re
import sys

path = "tests/test_release_catalog.cpp"
with open(path, encoding="utf-8") as fh:
    text = fh.read()

removed = []
for name in ("picksPlatformAsset", "pickUnknownReturnsEmpty"):
    pattern = re.compile(r"\n    void %s\(\)\n    \{.*?\n    \}\n" % name, re.S)
    text, n = pattern.subn("\n", text, count=1)
    if n != 1:
        sys.exit("could not remove %s" % name)
    removed.append(name)

with open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
print("removed: %s" % ", ".join(removed))