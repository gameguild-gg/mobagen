from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def is_ignored(path: str) -> bool:
    result = subprocess.run(
        ["git", "check-ignore", "--quiet", "--no-index", path],
        cwd=ROOT,
        check=False,
    )
    return result.returncode == 0


class AssetIgnorePolicyTest(unittest.TestCase):
    def test_generated_and_restricted_assets_are_ignored(self) -> None:
        for path in (
            "apps/dicom_viewer/assets/cache/head.zip",
            "apps/dicom_viewer/assets/head256x256x109.raw",
            "apps/dicom_viewer/assets/volume.raw",
            "apps/dicom_viewer/assets/volume.mvol",
            "apps/dicom_viewer/assets/dicom/study/series/slice.dcm",
        ):
            with self.subTest(path=path):
                self.assertTrue(is_ignored(path), path)

    def test_policy_files_are_not_ignored(self) -> None:
        for path in (
            "apps/dicom_viewer/assets/assets.json",
            "apps/dicom_viewer/assets/README.md",
        ):
            with self.subTest(path=path):
                self.assertFalse(is_ignored(path), path)


if __name__ == "__main__":
    unittest.main()
