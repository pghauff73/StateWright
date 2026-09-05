#!/usr/bin/env python3
"""Offline, pointwise checks only. Never imports or executes acquired source.

Run in the hash-pinned environment from math-verification-requirements.txt.
Input is JSON data, not an expression language. This does not issue SAA
qualification or promotion records and does not prove an algorithm correct.
"""
import argparse
from fractions import Fraction
import hashlib
from importlib import metadata
import json
from pathlib import Path
import re
import sys

PINNED = {"mpmath": "1.4.1", "python-flint": "0.9.0"}
FUNCTIONS = {"exp", "log", "sqrt", "sin", "cos", "gamma", "erf", "zeta"}
DECIMAL = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d{1,3})?\Z")


def decimal(value):
    if not isinstance(value, str) or len(value) > 128 or not DECIMAL.fullmatch(value):
        raise ValueError("finite bounded decimal string required")
    return Fraction(value)


def backend_provenance():
    result = {}
    for name, expected in PINNED.items():
        dist = metadata.distribution(name)
        if dist.version != expected:
            raise ValueError(f"{name} must be {expected}, found {dist.version}")
        notices = []
        for file in dist.files or []:
            if "licenses/" in str(file):
                data = Path(dist.locate_file(file)).read_bytes()
                notices.append({"path": str(file), "sha256": hashlib.sha256(data).hexdigest()})
        if not notices:
            raise ValueError(f"missing retained license notices for {name}")
        result[name] = {"version": dist.version, "license_notices": notices}
    return result


def validate_case(case):
    if not isinstance(case, dict) or set(case) != {
        "case_id", "function", "input", "candidate_value", "absolute_error_bound"
    }:
        raise ValueError("case fields must match the explicit pointwise protocol")
    if not isinstance(case["case_id"], str) or not 1 <= len(case["case_id"]) <= 120:
        raise ValueError("invalid case_id")
    function = case["function"]
    if not isinstance(function, str) or function not in FUNCTIONS:
        raise ValueError("unsupported function; expressions and code are not accepted")
    x = decimal(case["input"])
    candidate = decimal(case["candidate_value"])
    error = decimal(case["absolute_error_bound"])
    if abs(x) > 100 or abs(candidate) > 10**200:
        raise ValueError("point outside bounded verification scope")
    if error <= 0 or error > Fraction(1, 10**12):
        raise ValueError("absolute error budget must be in (0, 1e-12]")
    if function in {"log", "gamma"} and x <= 0:
        raise ValueError("positive-real domain required")
    if function == "sqrt" and x < 0:
        raise ValueError("nonnegative-real principal square root required")
    if function == "zeta" and x < 2:
        raise ValueError("zeta verification is restricted to real x >= 2")


def check_case(case, mp, arb, ctx):
    validate_case(case)
    name, x = case["function"], case["input"]
    function = getattr(mp, name)  # bounded by FUNCTIONS, not source-provided code
    with mp.workdps(80):
        reference80 = function(mp.mpf(x))
    with mp.workdps(120):
        reference = function(mp.mpf(x))
        budget = mp.mpf(case["absolute_error_bound"])
        error = abs(reference - mp.mpf(case["candidate_value"]))
        stable = abs(reference - reference80) <= budget / 4
        numerical_ok = bool(stable and error <= budget and mp.isfinite(reference))
        numerical = {"value": mp.nstr(reference, 115),
                     "absolute_error": mp.nstr(error, 15),
                     "precision_stable": bool(stable), "passed": numerical_ok}
    with ctx.workprec(384):
        enclosure = getattr(arb(x), name)()
        # A directed upper bound must fit inside the claimed error budget.
        # Merely overlapping intervals is insufficient.
        bound = (enclosure - arb(case["candidate_value"])).abs_upper()
        rigorous_ok = bool(enclosure.is_finite() and bound <= arb(case["absolute_error_bound"]))
        rigorous = {"reference_enclosure": enclosure.str(110),
                    "absolute_error_upper_bound": bound.str(110),
                    "passed": rigorous_ok, "precision_bits": 384}
    return {**case, "status": "PASS" if numerical_ok and rigorous_ok else "FAIL",
            "domain": "real; positive for log/gamma; nonnegative for sqrt; zeta >= 2",
            "branch_convention": "real-valued branch; principal nonnegative sqrt",
            "mpmath": numerical, "flint_arb": rigorous,
            "scope": "one input point only; not an algorithm qualification"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, help="new evidence file (never overwritten)")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("refusing to overwrite evidence")
    if args.input.stat().st_size > 256 * 1024:
        parser.error("input exceeds 256 KiB")
    raw = args.input.read_bytes()
    request = json.loads(raw)
    if not isinstance(request, dict) or set(request) != {"cases"} or not isinstance(request["cases"], list):
        parser.error("expected a cases array")
    if not 1 <= len(request["cases"]) <= 100:
        parser.error("expected 1..100 cases")
    provenance = backend_provenance()
    import mpmath
    import flint
    if flint.__FLINT_VERSION__ != "3.6.0":
        raise ValueError("expected the pinned wheel's FLINT 3.6.0 backend")
    results, ids = [], set()
    for case in request["cases"]:
        try:
            validate_case(case)
            if case["case_id"] in ids:
                raise ValueError("duplicate case_id")
            ids.add(case["case_id"])
            results.append(check_case(case, mpmath.mp, flint.arb, flint.ctx))
        except (ValueError, TypeError, KeyError, OverflowError) as error:
            results.append({"case_id": case.get("case_id") if isinstance(case, dict) else None,
                            "status": "REJECTED", "reason": str(error)})
    report = {"schema_version": 1, "verifier_version": "saa-math-pointwise-v1",
              "input_sha256": hashlib.sha256(raw).hexdigest(),
              "backends": provenance, "flint_version": flint.__FLINT_VERSION__,
              "independence": "cross-checks may share algorithmic ancestry; not independent proofs",
              "algorithm_qualification": "NOT_PERFORMED", "results": results}
    report["passed"] = all(r["status"] == "PASS" for r in results)
    with args.output.open("x") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
