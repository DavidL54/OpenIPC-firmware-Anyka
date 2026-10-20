"""Exercise log flooding, split/binary input, restart and process termination."""
import pathlib
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent
HEADER = b"Video encoder open success [observed before log rotation]\n"
with tempfile.TemporaryDirectory(prefix="anyka-sdk-log-") as directory:
    work = pathlib.Path(directory)
    binary = work / "logger"
    subprocess.run(["gcc", "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror",
                    "-o", str(binary), str(ROOT / "anyka-sdk-log.c")], check=True)
    fixture = work / "fixture.py"
    launcher = work / "start.sh"
    launcher.write_text(f"exec python3 '{fixture}'\n")
    log = work / "sdk.log"

    fixture.write_text("import os,time\n"
                       "os.write(1,b'Video encoder open ');time.sleep(.05)\n"
                       "os.write(2,b'success\\n')\n"
                       "for i in range(4000): os.write(1,('%06d '%i).encode()+b'x'*493+b'\\x00\\n')\n"
                       "raise SystemExit(7)\n")
    assert subprocess.run([str(binary), str(log), str(launcher)], timeout=10).returncode == 7
    assert 0 < log.stat().st_size <= 65536
    previous = work / "sdk.log.1"
    assert 0 < previous.stat().st_size <= 65536
    assert log.read_bytes().startswith(HEADER)
    assert b"003999 " in log.read_bytes()
    assert b"\x00" in log.read_bytes()
    print("PASS: 2 MB flood, split witness, binary bytes, recent messages, exit status, 128 KiB bound")

    fixture.write_text("import os\nfor i in range(200): os.write(1,b'z'*1000)\n")
    assert subprocess.run([str(binary), str(log), str(launcher)], timeout=10).returncode == 0
    assert b"Video encoder open success" not in log.read_bytes()
    assert b"Video encoder open success" not in previous.read_bytes()
    print("PASS: restart discards old readiness witness")

    fixture.write_text("import os,time\nwhile True:\n os.write(1,b'live\\n');time.sleep(.01)\n")
    process = subprocess.Popen([str(binary), str(log), str(launcher)])
    time.sleep(.2)
    process.terminate()
    assert process.wait(timeout=3) == 143
    print("PASS: TERM stops SDK child and logger")
