"""Lighting bridge regression checks with mocked game APIs (not a headset test)."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'build/test-deps'))
from lupa import LuaRuntime
lua = LuaRuntime(unpack_returned_tuples=True)
for path in (root / 'payload').rglob('*.lua'):
    result = lua.eval('function(s) return load(s) end')(path.read_text(encoding='utf-8-sig'))
    assert not isinstance(result, tuple), (path, result)
source = (root / 'payload/Survival/Scripts/game/Chapter2VR.lua').read_text()
body = source.split('local function materialLighting( active )', 1)[1].split('local function publishPlayerState', 1)[0]
lua.execute('''
Chapter2VR = {}; state = {cycle=0.5,tick=100}; blocked=false
local vector = {}; local mt = {__add=function(a,b) return vector end, __mul=function(a,b) return vector end}
setmetatable(vector, mt); vector.normalize=function() return vector end
sm={render={getOutdoorLighting=function() return 0 end,isOutdoor=function() return true end},
json={open=function() return state end}, game={getCurrentTick=function() return 100 end},
camera={getPosition=function() return vector end},localPlayer={getPlayer=function() return {} end},
vec3={new=function() return vector end},physics={raycast=function() return blocked end}, log={warning=function() end}}
''')
fn = lua.execute('local function materialLighting( active )' + body + '\nreturn materialLighting')
assert fn(True) == (1.0, 1, 1), 'renderer noon must override stuck-zero getter'
lua.execute('state.cycle=0')
assert fn(True)[0] == 0, 'night must dim'
lua.execute('state.cycle=0.5; blocked=true')
assert fn(True) == (1.0, 0, 0.25), 'cover must reduce direct and sky light'
lua.execute('state.tick=0')
assert fn(True)[0] == 0, 'stale renderer packet must not override getter'
print('PASS Lua payload syntax, noon, night, cover, stale packet')
