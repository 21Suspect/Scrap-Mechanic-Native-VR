// Dump decompiled callers of Windows mouse/input imports.
// @category ScrapMechanicVR

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class DumpInputCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) throw new IllegalArgumentException("usage: DumpInputCallers.java OUTPUT.txt");
        String[] names = {"GetRawInputData", "RegisterRawInputDevices", "GetCursorPos", "SetCursorPos",
                          "PeekMessageW", "DispatchMessageW", "GetAsyncKeyState", "GetFocus"};
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        Set<String> seen = new HashSet<>();
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(arguments[0]))) {
            for (String name : names) {
                writer.write("\n===== " + name + " =====\n");
                SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols(name);
                while (symbols.hasNext()) {
                    Symbol symbol = symbols.next();
                    writer.write("symbol " + symbol.getName(true) + " @ " + symbol.getAddress() + "\n");
                    ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(symbol.getAddress());
                    while (references.hasNext()) {
                        Reference reference = references.next();
                        Function caller = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
                        if (caller == null) continue;
                        String key = caller.getEntryPoint().toString();
                        writer.write("xref " + reference.getFromAddress() + " caller " + caller.getName() + " @ " + key + "\n");
                        if (!seen.add(key)) continue;
                        DecompileResults result = decompiler.decompileFunction(caller, 90, monitor);
                        if (result.decompileCompleted()) writer.write(result.getDecompiledFunction().getC() + "\n");
                        else writer.write("decompile failed: " + result.getErrorMessage() + "\n");
                    }
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
