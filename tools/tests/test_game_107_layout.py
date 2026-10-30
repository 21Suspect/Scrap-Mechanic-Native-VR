"""Check every guarded hook/input site against the actual 1.0.7 executable.

Requires pefile, and a legally installed Scrap Mechanic 1.0.7.894 executable.
This reads the binary only; it does not bypass the runtime's build gate.
"""
import hashlib
from pathlib import Path
import re
import struct
import sys

import pefile

root = Path(__file__).resolve().parents[2]
exe = Path(sys.argv[1])
assert hashlib.sha256(exe.read_bytes()).hexdigest().upper() == (
    'C5D93162DB87605CBB70598B347634F555A2F7C3658225963C5ED8F97F68EEAA')
pe = pefile.PE(str(exe))
image = pe.get_memory_mapped_image()
assert pe.FILE_HEADER.TimeDateStamp == 0x6AE372FB
assert pe.OPTIONAL_HEADER.SizeOfImage == 0x1C2C000

def constants(text):
    return {k: int(v, 16) for k, v in re.findall(r'constexpr \w+ (\w+) = 0x([0-9A-Fa-f]+);', text)}

def check(addr, expected, label):
    actual = image[addr:addr + len(expected)]
    assert actual == expected, f'{label} at {addr:x}: {actual.hex()} != {expected.hex()}'

native = (root / 'src/native_vr.cpp').read_text()
c = constants(native)
arrays = {name: bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
          for name, body in re.findall(r'constexpr uint8_t (\w+)\[\] = \{(.*?)\};', native, re.S)}
for name, rva in [('kRenderPrefix', 'kRenderSetupRva'), ('kCameraBuildPrefix', 'kCameraBuildRva'),
                  ('kRaycastPrefix', 'kRaycastRva'), ('kViewmodelConditionalBranch', 'kViewmodelSkipBranchRva'),
                  ('kRaycastResultFractionStore', 'kRaycastResultFractionStoreRva')]:
    check(c[rva], arrays[name], name)
check(c['kPlayerToolRaycastReturnRva'] - len(arrays['kPlayerToolRaycastCall']), arrays['kPlayerToolRaycastCall'], 'player ray')

def call_target(at):
    assert image[at] == 0xE8
    return at + 5 + struct.unpack_from('<i', image, at + 1)[0]

assert call_target(c['kFirstPersonCameraCallerRva'] - 5) == c['kCameraBuildRva']
assert call_target(c['kPlayerToolRaycastReturnRva'] - 5) == c['kRaycastRva']

source = (root / 'src/feature_engine_input.cpp').read_text()
c = constants(source)
guards = re.findall(r'bytes_equal\(base, (\w+), (0x[0-9A-Fa-f]+),\s*std::array<uint8_t, (\d+)>\{([^}]+)\}', source)
assert len(guards) == 16, len(guards)
for name, offset, length, body in guards:
    expected = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    assert len(expected) == int(length)
    check(c[name] + int(offset, 16), expected, name)
for name, offset in [('kKeyboardEventRva',0xE2),('kMouseDownRva',0x6F),('kMouseUpRva',0x73),('kMouseMoveRva',0x8D)]:
    assert call_target(c[name] + offset) == c['kQueueEventRva']
at = c['kInputManagerLoadRva']
assert at + 7 + struct.unpack_from('<i', image, at + 3)[0] == c['kInputManagerPointerRva']
print(f'PASS: native render/camera/ray sites, {len(guards)} input guards, input call targets and singleton; 1.0.7.894')
