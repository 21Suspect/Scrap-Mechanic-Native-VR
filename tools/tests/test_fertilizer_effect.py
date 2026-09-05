"""Run the shipped Fertilizer Lua against mocked local/remote tool owners.

Usage: python tools/tests/test_fertilizer_effect.py --lua-dll <game>/Release/lua51.dll
"""
import argparse
import ctypes
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--lua-dll', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'payload/Survival/Scripts/game/tools/Fertilizer.lua').read_text(encoding='utf-8')
setup = r'''
dofile = function() end
class = function() return {} end
localCount, hostedCount, poseCount, fpCount = 0, 0, 0, 0
localCharacter, remoteCharacter = {}, {}
sm = {
    tool = { preloadRenderables = function() end },
    vec3 = { new = function() return {} end, getRotation = function() return {} end },
    localPlayer = { getFpBonePos = function() return nil end },
    effect = {
        playEffect = function(name, position)
            assert(name == 'Itemtool - FPFertilizerUse' and position == 'localHand')
            localCount = localCount + 1
        end,
        playHostedEffect = function(name, owner, bone)
            assert(name == 'Itemtool - FertilizerUse' and bone == 'jnt_fertilizer')
            lastHostedOwner = owner
            hostedCount = hostedCount + 1
        end
    }
}
setFpAnimation = function() fpCount = fpCount + 1 end
setTpAnimation = function() end
Chapter2VR = { actionPose = function()
    poseCount = poseCount + 1
    return {position = 'localHand', direction = {}}, true
end }
function makeTool(isLocal, character, firstPerson)
    return { tool = {
        isLocal = function() return isLocal end,
        isEquipped = function() return true end,
        isInFirstPersonView = function() return firstPerson end,
        getOwner = function() return {getCharacter = function() return character end} end
    }, onUse = function(self) Fertilizer.onUse(self) end }
end
'''
checks = r'''
local remote = makeTool(false, remoteCharacter, false)
Fertilizer.cl_n_onUse(remote)
assert(poseCount == 0 and localCount == 0 and hostedCount == 1)
assert(lastHostedOwner == remoteCharacter and fpCount == 0)
local player = makeTool(true, localCharacter, true)
Fertilizer.onUse(player)
assert(poseCount == 1 and localCount == 1 and fpCount == 1)
Fertilizer.cl_n_onUse(player) -- network echo must not duplicate the local effect
assert(localCount == 1)
Chapter2VR = nil
Fertilizer.onUse(makeTool(true, localCharacter, false))
assert(hostedCount == 2 and lastHostedOwner == localCharacter)
Fertilizer.cl_n_onUse(remote)
assert(hostedCount == 3 and lastHostedOwner == remoteCharacter)
Fertilizer.onUse(player) -- first-person desktop fallback with unavailable bone
assert(localCount == 1 and poseCount == 1)
'''
lua = ctypes.CDLL(str(args.lua_dll.resolve()))
lua.luaL_newstate.restype = ctypes.c_void_p
lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
lua.luaL_loadstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lua.lua_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
lua.lua_tolstring.restype = ctypes.c_char_p
lua.lua_close.argtypes = [ctypes.c_void_p]
state = lua.luaL_newstate()
assert state
try:
    lua.luaL_openlibs(state)
    result = lua.luaL_loadstring(state, (setup + source + checks).encode('utf-8'))
    if result == 0:
        result = lua.lua_pcall(state, 0, 0, 0)
    if result != 0:
        raise AssertionError(lua.lua_tolstring(state, -1, None).decode('utf-8'))
finally:
    lua.lua_close(state)
print('PASS: remote ownership, local VR, network echo, desktop fallback')
