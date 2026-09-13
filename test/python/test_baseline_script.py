from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "baseline.py"


class BaselineScriptTest(unittest.TestCase):
    def run_capture(
        self,
        fixture: str,
        *,
        existing_output: bytes | None = None,
    ) -> tuple[subprocess.CompletedProcess[str], bytes | None]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            fixture_path = temporary_path / "benchmark_fixture.py"
            fixture_path.write_text(textwrap.dedent(fixture), encoding="utf-8")
            output_path = temporary_path / "baseline.json"
            if existing_output is not None:
                output_path.write_bytes(existing_output)

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--binary",
                    str(fixture_path),
                    "--output",
                    str(output_path),
                    "--configuration",
                    "Test",
                    "--compiler",
                    "fixture",
                    "--warmup",
                    "2",
                    "--samples",
                    "3",
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            output = output_path.read_bytes() if output_path.exists() else None
            return result, output

    def test_rejects_non_json_benchmark_output_without_replacing_evidence(self) -> None:
        sentinel = b"previous evidence\n"

        result, output = self.run_capture("print('not json')", existing_output=sentinel)

        self.assertEqual(result.returncode, 1)
        self.assertIn("benchmark did not emit valid JSON", result.stderr)
        self.assertEqual(output, sentinel)

    def test_rejects_failed_benchmark_without_replacing_evidence(self) -> None:
        sentinel = b"previous evidence\n"

        result, output = self.run_capture(
            """
            import sys
            print("fixture failed", file=sys.stderr)
            raise SystemExit(7)
            """,
            existing_output=sentinel,
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("benchmark exited with code 7", result.stderr)
        self.assertEqual(output, sentinel)

    def test_wraps_valid_benchmark_with_provenance(self) -> None:
        result, output = self.run_capture(
            """
            import json

            print(json.dumps({
                "schema": "mobagen.foundation-benchmark.v1",
                "warmup": 2,
                "samples": 3,
                "results": [{
                    "name": "fixture",
                    "median_ns": 20.0,
                    "p95_ns": 30.0,
                    "samples_ns": [10.0, 20.0, 30.0],
                }],
            }))
            """
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNotNone(output)
        baseline = json.loads(output)
        self.assertEqual(baseline["schema"], "mobagen.baseline.v1")
        self.assertEqual(
            baseline["benchmark"]["schema"], "mobagen.foundation-benchmark.v1"
        )
        self.assertEqual(baseline["benchmark"]["samples"], 3)
        self.assertIn("captured_at_utc", baseline)
        self.assertIn("commit", baseline["git"])
        self.assertIsInstance(baseline["git"]["dirty"], bool)
        self.assertIn("system", baseline["platform"])
        self.assertEqual(baseline["toolchain"]["configuration"], "Test")
        self.assertEqual(baseline["toolchain"]["compiler"], "fixture")
        self.assertIn("python", baseline["toolchain"])
        self.assertIn("cmake", baseline["toolchain"])
        self.assertEqual(baseline["command"][-4:], ["--warmup", "2", "--samples", "3"])


if __name__ == "__main__":
    unittest.main()
