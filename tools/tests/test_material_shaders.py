"""Compile the actual embedded hand/tool HLSL with Windows D3DCompiler."""
import ctypes as c
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
compiler = c.WinDLL('d3dcompiler_47.dll').D3DCompile
compiler.argtypes = [c.c_void_p, c.c_size_t, c.c_char_p, c.c_void_p, c.c_void_p,
                     c.c_char_p, c.c_char_p, c.c_uint, c.c_uint,
                     c.POINTER(c.c_void_p), c.POINTER(c.c_void_p)]
compiler.restype = c.c_long

def blob_method(blob, index, result):
    table = c.cast(blob, c.POINTER(c.POINTER(c.c_void_p))).contents
    return c.WINFUNCTYPE(result, c.c_void_p)(table[index])(blob)

for name in ('vr_tools', 'vr_hands'):
    source = (root / 'source/NativeVR/src' / (name + '.cpp')).read_text()
    assert 'context->PSSetConstantBuffers(0, 1, &g_constant_buffer)' in source, 'pixel lighting buffer must be bound'
    if name == 'vr_hands':
        assert 'context->PSGetConstantBuffers(0, 1, &pixel_constant)' in source
        assert 'context->PSSetConstantBuffers(0, 1, &pixel_constant)' in source
    shader = re.search(r'const char \*shader = R"\((.*?)\)";', source, re.S).group(1).encode()
    entries = ['vs_main', 'ps_main'] + (['ps_cutout', 'ps_laser', 'ps_target'] if name == 'vr_tools' else [])
    for entry in entries:
        code, errors = c.c_void_p(), c.c_void_p()
        hr = compiler(shader, len(shader), name.encode(), None, None, entry.encode(),
                      ('vs_5_0' if entry.startswith('vs') else 'ps_5_0').encode(), 0, 0,
                      c.byref(code), c.byref(errors))
        message = c.string_at(blob_method(errors, 3, c.c_void_p)).decode() if errors else ''
        if errors: blob_method(errors, 2, c.c_ulong)
        if code: blob_method(code, 2, c.c_ulong)
        assert hr >= 0, (name, entry, message)
        print('PASS', name, entry)
