import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stress", ROOT / "stress.py")
stress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stress)


class StressRunnerTest(unittest.TestCase):
    def test_trends(self):
        self.assertEqual(stress.trend([]), 0)
        self.assertEqual(stress.trend([(0, 100), (10, 100)]), 0)
        self.assertAlmostEqual(stress.trend([(0, 100), (10, 110), (20, 120)]), 60)

    def test_resource_sampling(self):
        import os
        rss, descriptors = stress.resources(os.getpid())
        self.assertGreater(rss, 0)
        self.assertGreater(descriptors, 0)

    def test_bounded_arguments(self):
        for flag, value in (("--seconds", "0"), ("--seconds", "121"),
                            ("--time-scale", "0"), ("--rss-growth-mib", "-1"),
                                                        ("--soak-seconds", "86401"), ("--warmup-seconds", "601")):
            result = subprocess.run(["python3", str(ROOT / "stress.py"), flag, value],
                                    capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 2)

    def test_cli_numeric_rejection(self):
        binary = ROOT / "build/deskmate-emulator"
        for flag in ("--heap-bytes", "--max-block-bytes", "--stack-bytes",
                     "--flash-bytes", "--time-scale", "--duration-ms",
                     "--network-fail-every", "--truncate-every", "--web-port"):
            for value in ("-1", "garbage", "12x", "4294967296"):
                result = subprocess.run([str(binary), flag, value],
                                        capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 1, (flag, value))


if __name__ == "__main__":
    unittest.main()
