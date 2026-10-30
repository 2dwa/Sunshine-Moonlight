# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
from pathlib import Path
import struct
import subprocess

root = Path(__file__).resolve().parent.parent
probe = root / 'out/tests/test-console-startup.exe'
sunshine = root / 'out/sunshine/sunshine.exe'

def subsystem(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0'
    return struct.unpack_from('<H', data, pe + 24 + 68)[0]

for executable in (probe, sunshine):
    assert subsystem(executable) == 2, f'{executable.name} is not a Windows GUI executable'

# No CREATE_NO_WINDOW or hidden-window flags: the executable itself must not
# allocate a console. The probe uses the exact production startup helper.
for arguments in ([], ['--diagnostic']):
    result = subprocess.run([str(probe), *arguments], capture_output=True, text=True,
                            timeout=10, check=True)
    assert result.stdout == 'stdout-preserved\n'
    assert result.stderr == 'stderr-preserved\n'

result = subprocess.run([str(sunshine), '--version'], capture_output=True, text=True,
                        timeout=15, check=True)
assert 'Sunshine version: 2026.1020.23431' in result.stdout
print('PASS: GUI subsystem; no console allocation; stdout/stderr redirection; Sunshine --version')
