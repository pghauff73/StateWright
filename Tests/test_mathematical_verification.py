"""Optional dependency-backed tests; core StateWright remains Python-free."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
SCRIPT = ROOT / "Tools/verify_mathematical_candidates.py"
spec = importlib.util.spec_from_file_location("math_verifier", SCRIPT)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class VerificationTests(unittest.TestCase):
    def run_cases(self, cases):
        with tempfile.TemporaryDirectory(prefix="statewright-math-test-") as temp:
            request, result = Path(temp) / "request.json", Path(temp) / "result.json"
            request.write_text(json.dumps({"cases": cases}))
            process = subprocess.run([sys.executable, str(SCRIPT), str(request), str(result)],
                                     capture_output=True, text=True, timeout=30)
            report = json.loads(result.read_text()) if result.exists() else None
            return process, report

    def example(self):
        return {"case_id": "sqrt", "function": "sqrt", "input": "4",
                "candidate_value": "2", "absolute_error_bound": "1e-40"}

    def test_known_values_both_backends(self):
        cases = json.loads((ROOT / "Tests/fixtures/mathematical-points-v1.json").read_text())["cases"]
        process, report = self.run_cases(cases)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertTrue(report["passed"])
        self.assertEqual(report["algorithm_qualification"], "NOT_PERFORMED")
        self.assertTrue(all(r["mpmath"]["passed"] and r["flint_arb"]["passed"] for r in report["results"]))

    def test_incorrect_candidate_fails_both_backends(self):
        case = self.example()
        case["candidate_value"] = "3"
        process, report = self.run_cases([case])
        self.assertEqual(process.returncode, 1)
        self.assertEqual(report["results"][0]["status"], "FAIL")
        self.assertFalse(report["results"][0]["flint_arb"]["passed"])

    def test_domain_code_nonfinite_and_weak_budget_rejected(self):
        cases = []
        for field, value in [("input", "-1"), ("input", "NaN"),
                             ("function", "__import__('os')"),
                             ("absolute_error_bound", "1"), ("absolute_error_bound", "-1"),
                             ("candidate_value", "Infinity")]:
            case = self.example()
            case[field] = value
            case["case_id"] = str(len(cases))
            cases.append(case)
        process, report = self.run_cases(cases)
        self.assertEqual(process.returncode, 1)
        self.assertTrue(all(r["status"] == "REJECTED" for r in report["results"]))

    def test_duplicate_points_rejected(self):
        process, report = self.run_cases([self.example(), self.example()])
        self.assertEqual(process.returncode, 1)
        self.assertEqual(report["results"][1]["reason"], "duplicate case_id")

    def test_excessive_cases_rejected(self):
        process, report = self.run_cases([self.example()] * 101)
        self.assertNotEqual(process.returncode, 0)
        self.assertIsNone(report)

    def test_wrong_backend_version_rejected(self):
        with patch.object(verifier.metadata, "distribution") as distribution:
            distribution.return_value.version = "unreviewed"
            with self.assertRaises(ValueError):
                verifier.backend_provenance()


if __name__ == "__main__":
    unittest.main()
