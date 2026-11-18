# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "support"))
from runtime import validated_scratch

with TemporaryDirectory(dir=validated_scratch(), prefix="batch-model-") as directory:
    result = subprocess.run([sys.argv[1], directory], check=False)
assert not Path(directory).exists()
raise SystemExit(result.returncode)
