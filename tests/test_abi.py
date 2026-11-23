# FILE: tests/test_abi.py
# WHAT: checks that every SC_ export declared in sc_api.h is defined in dllmain.cpp with SHIP_CONTROL_API, and the reverse.
# WHY: the C ABI is the contract other mods load by name; a renamed or missing export breaks them at runtime, and CI builds no DLLs (static checks only).
# RUN: python -I tests\test_abi.py (run.ps1 runs it). Prints "ok ..." / "FAIL ..." lines; exit 0 = all ok.
import os
import re
import sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
with open(os.path.join(root, "sc_api.h"), encoding="utf-8") as f:
    header = set(re.findall(r"^\s*(?:int|void)\s+(SC_\w+)\s*\(", f.read(), re.M))
with open(os.path.join(root, "dllmain.cpp"), encoding="utf-8") as f:
    exported = set(re.findall(r"SHIP_CONTROL_API\s+[\w:*<> ]+?\b(SC_\w+)\s*\(", f.read()))

fails = 0
if not header:
    print("FAIL no SC_ declarations found in sc_api.h")
    fails += 1
for name in sorted(header - exported):
    print(f"FAIL {name} declared in sc_api.h but not exported from dllmain.cpp")
    fails += 1
for name in sorted(exported - header):
    print(f"FAIL {name} exported from dllmain.cpp but not declared in sc_api.h")
    fails += 1
# API 5: the owner-scoped exports exist and SC_api() returns 8
for name in ("SC_set_host_for", "SC_set_tune_for", "SC_tick_speed_for", "SC_tick_frame_for", "SC_tick_bubble_for"):
    if name not in header or name not in exported:
        print(f"FAIL {name} missing (API 5)")
        fails += 1
with open(os.path.join(root, "sc_api.h"), encoding="utf-8") as f:
    lvl = re.search(r"#define SC_API_LEVEL\s+(\d+)", f.read())
with open(os.path.join(root, "dllmain.cpp"), encoding="utf-8") as f:
    dll = f.read()
ret = re.search(r"SC_api\(\)[^}]*?return\s+(\d+)\s*;", dll)
# legacy exports must be one-line wrappers of the _for export with owner "mining" (kLegacyOwner)
if 'kLegacyOwner = "mining"' not in dll:
    print("FAIL kLegacyOwner is not mining")
    fails += 1
for legacy, new in (("SC_set_host", "SC_set_host_for"), ("SC_set_tune", "SC_set_tune_for"), ("SC_tick_speed", "SC_tick_speed_for"),
                    ("SC_tick_frame", "SC_tick_frame_for"), ("SC_tick_bubble", "SC_tick_bubble_for")):
    if not re.search(r"SHIP_CONTROL_API int " + legacy + r"\([^)]*\)\s*\{\s*return " + new + r"\(kLegacyOwner,", dll):
        print(f"FAIL {legacy} is not a wrapper of {new}(kLegacyOwner, ...)")
        fails += 1
if not lvl or lvl.group(1) != "8" or not ret or ret.group(1) != "8":
    print("FAIL SC_api() / SC_API_LEVEL is not 8")
    fails += 1
if not fails:
    print(f"ok {len(header)} SC_ exports match sc_api.h and dllmain.cpp")
sys.exit(1 if fails else 0)
