"""Kill a writer after persisting selected prefixes of a real WAL record."""
import os
from pathlib import Path
import signal
import subprocess
import sys
from tempfile import TemporaryDirectory

cli = Path(sys.argv[1]).resolve()
with TemporaryDirectory() as scratch:
    root = Path(scratch)
    base = root / "base"
    subprocess.run([cli, base, "set", "stable", "yes"], check=True)
    wal = base / "store.wal"
    old_log = wal.read_bytes()
    subprocess.run([cli, base, "set", "interrupted", "payload"], check=True)
    new_record = wal.read_bytes()[len(old_log):]

    for cut in range(len(new_record) + 1):
        trial = root / str(cut)
        trial.mkdir()
        trial_log = trial / "store.wal"
        trial_log.write_bytes(old_log)
        read_ready, write_ready = os.pipe()
        child = os.fork()
        if child == 0:
            os.close(read_ready)
            with trial_log.open("ab", buffering=0) as output:
                output.write(new_record[:cut])
                os.fsync(output.fileno())
                os.write(write_ready, b"1")
                signal.pause()
            os._exit(1)
        os.close(write_ready)
        ready = os.read(read_ready, 1)
        os.close(read_ready)
        if ready:
            os.kill(child, signal.SIGKILL)
        _, status = os.waitpid(child, 0)
        assert ready and os.WTERMSIG(status) == signal.SIGKILL
        assert subprocess.check_output([cli, trial, "get", "stable"]) == b"yes\n"
        result = subprocess.run([cli, trial, "get", "interrupted"], capture_output=True)
        if cut == len(new_record):
            assert result.returncode == 0 and result.stdout == b"payload\n"
        else:
            assert result.returncode == 2 and trial_log.read_bytes() == old_log
        subprocess.run([cli, trial, "set", "later", "yes"], check=True)
        assert subprocess.check_output([cli, trial, "get", "later"]) == b"yes\n"
