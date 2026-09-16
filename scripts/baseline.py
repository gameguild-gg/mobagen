#!/usr/bin/env python3
"""Capture a Mobagen foundation benchmark with reproducibility metadata."""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn, Sequence


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK_SCHEMA = "mobagen.foundation-benchmark.v1"
BASELINE_SCHEMA = "mobagen.baseline.v1"


class CaptureError(RuntimeError):
    """A benchmark or provenance contract could not be captured."""


def positive_count(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("requires a positive integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("requires a positive integer")
    return parsed


def parse_arguments(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--warmup", required=True, type=positive_count)
    parser.add_argument("--samples", required=True, type=positive_count)
    return parser.parse_args(arguments)


def command_for_binary(binary: Path, warmup: int, samples: int) -> list[str]:
    command = [str(binary), "--warmup", str(warmup), "--samples", str(samples)]
    if binary.suffix.casefold() == ".py":
        command.insert(0, sys.executable)
    return command


def run_text(command: Sequence[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def fail_invalid_json() -> NoReturn:
    raise CaptureError("benchmark did not emit valid JSON")


def parse_benchmark(output: str, warmup: int, samples: int) -> dict[str, Any]:
    try:
        payload = json.loads(output, parse_constant=lambda _value: fail_invalid_json())
    except (json.JSONDecodeError, TypeError):
        fail_invalid_json()

    if not isinstance(payload, dict) or payload.get("schema") != BENCHMARK_SCHEMA:
        raise CaptureError(f"benchmark schema must be {BENCHMARK_SCHEMA}")
    if payload.get("warmup") != warmup or payload.get("samples") != samples:
        raise CaptureError("benchmark options do not match the capture request")

    results = payload.get("results")
    if not isinstance(results, list) or not results:
        raise CaptureError("benchmark results must be a non-empty array")

    names: set[str] = set()
    for result in results:
        if not isinstance(result, dict):
            raise CaptureError("benchmark result must be an object")
        name = result.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise CaptureError("benchmark result names must be non-empty and unique")
        names.add(name)

        raw_samples = result.get("samples_ns")
        if not isinstance(raw_samples, list) or len(raw_samples) != samples:
            raise CaptureError(f"benchmark result {name!r} has an invalid sample count")
        numeric_values = [*raw_samples, result.get("median_ns"), result.get("p95_ns")]
        if any(
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
            or value < 0
            for value in numeric_values
        ):
            raise CaptureError(f"benchmark result {name!r} contains invalid timing values")

    return payload


def git_value(*arguments: str) -> str:
    result = run_text(["git", *arguments])
    if result.returncode != 0:
        raise CaptureError(f"git {' '.join(arguments)} failed")
    return result.stdout.strip()


def cmake_version() -> str:
    result = run_text(["cmake", "--version"])
    if result.returncode != 0 or not result.stdout.strip():
        raise CaptureError("cmake --version failed")
    return result.stdout.splitlines()[0].strip()


def collect_git_provenance() -> dict[str, Any]:
    return {
        "branch": git_value("branch", "--show-current"),
        "commit": git_value("rev-parse", "HEAD"),
        "dirty": bool(git_value("status", "--porcelain", "--untracked-files=normal")),
    }


def capture(arguments: argparse.Namespace) -> dict[str, Any]:
    git = collect_git_provenance()
    command = command_for_binary(arguments.binary, arguments.warmup, arguments.samples)
    benchmark_result = run_text(command)
    if benchmark_result.returncode != 0:
        detail = benchmark_result.stderr.strip()
        suffix = f": {detail}" if detail else ""
        raise CaptureError(f"benchmark exited with code {benchmark_result.returncode}{suffix}")
    benchmark = parse_benchmark(benchmark_result.stdout, arguments.warmup, arguments.samples)

    return {
        "benchmark": benchmark,
        "captured_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "command": command,
        "git": git,
        "platform": {
            "architecture": platform.machine(),
            "processor": platform.processor(),
            "release": platform.release(),
            "system": platform.system(),
        },
        "schema": BASELINE_SCHEMA,
        "toolchain": {
            "cmake": cmake_version(),
            "compiler": arguments.compiler,
            "configuration": arguments.configuration,
            "python": platform.python_version(),
        },
    }


def write_atomic(output: Path, payload: dict[str, Any]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main(arguments: Sequence[str] | None = None) -> int:
    options = parse_arguments(arguments)
    try:
        payload = capture(options)
        write_atomic(options.output, payload)
    except (CaptureError, OSError, UnicodeError, ValueError) as error:
        print(f"baseline capture failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
