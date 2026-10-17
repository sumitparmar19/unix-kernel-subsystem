#!/usr/bin/env python3
"""Fail if a suite's pass count dropped below its recorded baseline.

Usage: check_results.py EXPECTED.json ACTUAL_results.json

EXPECTED.json maps each suite to {"passed": N, "total": T}.  A run
passes if actual passed >= expected passed and total == expected total.
A suite not listed in EXPECTED is reported but does not fail CI.
"""
import json
import sys


def main():
    expected = json.load(open(sys.argv[1]))
    actual = json.load(open(sys.argv[2]))
    suite = actual["project"]
    got_pass, got_total = actual["passed"], actual["total"]

    if suite not in expected:
        print(f"{suite}: {got_pass}/{got_total} (no baseline recorded)")
        return 0
    want = expected[suite]
    ok = got_pass >= want["passed"] and got_total == want["total"]
    status = "OK" if ok else "REGRESSION"
    print(f"{suite}: {got_pass}/{got_total} "
          f"(baseline {want['passed']}/{want['total']}) {status}")
    if not ok:
        print(f"::error::{suite} regressed: {got_pass}/{got_total} < "
              f"{want['passed']}/{want['total']}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
