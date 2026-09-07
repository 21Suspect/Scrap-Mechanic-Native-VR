// Read-only decompilation of validated build tool-visual call sites.
// @category ScrapMechanicVR
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
public class DumpToolVisuals extends GhidraScript {
    public void run() throws Exception {
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try {
            for (String arg : getScriptArgs()) {
                Function f = getFunctionContaining(toAddr(arg));
                if (f == null) { println("NO FUNCTION " + arg); continue; }
                println("FUNCTION " + f.getEntryPoint());
                String code = d.decompileFunction(f, 60, monitor).getDecompiledFunction().getC();
                java.nio.file.Files.writeString(java.nio.file.Path.of(
                    "C:/Users/Fabian/ScrapMechanicVR/analysis/static/tool_" + f.getEntryPoint() + ".c"), code);
            }
        } finally { d.dispose(); }
    }
}
