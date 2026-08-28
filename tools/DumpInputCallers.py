# Ghidra headless script: dump decompiled callers of Windows mouse/input imports.
# @category ScrapMechanicVR

from ghidra.app.decompiler import DecompInterface
from java.io import FileWriter, BufferedWriter


names = [
    "GetRawInputData", "RegisterRawInputDevices", "GetCursorPos", "SetCursorPos",
    "PeekMessageW", "DispatchMessageW", "GetAsyncKeyState", "GetFocus"
]
arguments = getScriptArgs()
if len(arguments) != 1:
    raise RuntimeError("usage: DumpInputCallers.py OUTPUT.txt")

decompiler = DecompInterface()
decompiler.openProgram(currentProgram)
seen = set()
writer = BufferedWriter(FileWriter(arguments[0]))
try:
    table = currentProgram.getSymbolTable()
    references = currentProgram.getReferenceManager()
    functions = currentProgram.getFunctionManager()
    for name in names:
        writer.write("\n===== %s =====\n" % name)
        iterator = table.getSymbols(name)
        while iterator.hasNext():
            symbol = iterator.next()
            writer.write("symbol %s @ %s\n" % (symbol.getName(True), symbol.getAddress()))
            for reference in references.getReferencesTo(symbol.getAddress()):
                caller = functions.getFunctionContaining(reference.getFromAddress())
                if caller is None:
                    continue
                key = str(caller.getEntryPoint())
                writer.write("xref %s caller %s @ %s\n" %
                             (reference.getFromAddress(), caller.getName(), key))
                if key in seen:
                    continue
                seen.add(key)
                result = decompiler.decompileFunction(caller, 90, monitor)
                if result.decompileCompleted():
                    writer.write(result.getDecompiledFunction().getC())
                    writer.write("\n")
                else:
                    writer.write("decompile failed: %s\n" % result.getErrorMessage())
finally:
    writer.close()
    decompiler.dispose()
