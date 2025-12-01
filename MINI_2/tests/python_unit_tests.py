import json
import os
import stat
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS_DIR = ROOT / "scripts"
DATASET = ROOT / "test_data" / "data_10k.csv"
ESSENTIAL_SCRIPTS = [
    "build.sh",
    "gen_proto.sh",
    "start_node.sh",
    "start_servers.sh",
    "test_real_data.sh",
]


class ShellParityTests(unittest.TestCase):
    def test_network_config_structure(self):
        config_path = ROOT / "config" / "network_setup.json"
        with config_path.open() as fh:
            cfg = json.load(fh)

        nodes = cfg.get("nodes", [])
        self.assertEqual(len(nodes), 6, "expected exactly 6 nodes in network config")

        node_ids = {node["id"] for node in nodes}
        self.assertTrue(node_ids.issuperset({"A", "B", "C", "D", "E", "F"}))

        overlay = cfg.get("overlay", [])
        self.assertGreater(len(overlay), 0)
        for edge in overlay:
            self.assertEqual(len(edge), 2)
            self.assertTrue({edge[0], edge[1]}.issubset(node_ids))

    def test_scripts_have_shebang_and_exec_bit(self):
        for script in ESSENTIAL_SCRIPTS:
            path = SCRIPTS_DIR / script
            self.assertTrue(path.exists(), f"missing script: {script}")
            with path.open() as fh:
                first_line = fh.readline().strip()
            self.assertTrue(first_line.startswith("#!"), f"{script} missing shebang")
            mode = os.stat(path).st_mode
            self.assertTrue(mode & stat.S_IXUSR, f"{script} is not executable")

    def test_start_servers_help(self):
        result = subprocess.run(
            ["bash", str(SCRIPTS_DIR / "start_servers.sh"), "--help"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Usage: start_servers.sh", result.stdout)

    def test_real_data_help(self):
        result = subprocess.run(
            ["bash", str(SCRIPTS_DIR / "test_real_data.sh"), "--help"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("test_real_data.sh", result.stdout)

    def test_dataset_header_is_present(self):
        self.assertTrue(DATASET.exists(), "default dataset missing")
        with DATASET.open() as fh:
            header = fh.readline().strip()
        columns = [col for col in header.split(",") if col]
        self.assertGreaterEqual(len(columns), 3, "dataset header is unexpectedly short")


if __name__ == "__main__":
    unittest.main()
