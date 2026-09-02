// Exports a function inventory for the whole program, plus decompiled C for the
// call-graph neighborhood around one or more anchor addresses.
//
// Usage (headless):
//   -postScript ExportNeighborhood.java <outDir> <depth> <anchorVA> [anchorVA...]
//
//@category MarkerPatch

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class ExportNeighborhood extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("ERROR: need <outDir> <depth> <anchorVA> [more anchors...]");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();
        int depth = Integer.parseInt(args[1]);

        FunctionManager fm = currentProgram.getFunctionManager();
        String progTag = currentProgram.getName().replaceAll("[^A-Za-z0-9]", "_");

        // ---- 1. full function inventory (cheap: no decompilation) ----
        File invFile = new File(outDir, progTag + "_functions.csv");
        int nFuncs = 0;
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(invFile)))) {
            pw.println("address,name,size,params,calling,called");
            for (Function f : fm.getFunctions(true)) {
                if (monitor.isCancelled()) break;
                pw.printf("0x%s,%s,%d,%d,%d,%d%n",
                    f.getEntryPoint().toString(),
                    f.getName().replace(',', '_'),
                    f.getBody().getNumAddresses(),
                    f.getParameterCount(),
                    f.getCallingFunctions(monitor).size(),
                    f.getCalledFunctions(monitor).size());
                nFuncs++;
            }
        }
        println("Wrote inventory: " + invFile + "  (" + nFuncs + " functions)");

        // ---- 2. collect neighborhood around anchors ----
        Set<Function> wanted = new LinkedHashSet<>();
        List<Function> frontier = new ArrayList<>();
        for (int i = 2; i < args.length; i++) {
            Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
            Function f = fm.getFunctionContaining(a);
            if (f == null) { println("WARN: no function at " + args[i]); continue; }
            println("Anchor " + args[i] + " -> " + f.getName() + " @ " + f.getEntryPoint());
            wanted.add(f); frontier.add(f);
        }
        for (int d = 0; d < depth; d++) {
            List<Function> next = new ArrayList<>();
            for (Function f : frontier) {
                for (Function c : f.getCalledFunctions(monitor))  if (wanted.add(c)) next.add(c);
                for (Function c : f.getCallingFunctions(monitor)) if (wanted.add(c)) next.add(c);
            }
            frontier = next;
            println("  depth " + (d + 1) + ": " + wanted.size() + " functions total");
        }

        // ---- 3. decompile the neighborhood ----
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        File cDir = new File(outDir, progTag + "_decomp");
        cDir.mkdirs();
        int ok = 0, fail = 0;
        for (Function f : wanted) {
            if (monitor.isCancelled()) break;
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            if (r != null && r.decompileCompleted()) {
                String fn = String.format("%s_%s.c", f.getEntryPoint(),
                                          f.getName().replaceAll("[^A-Za-z0-9_]", "_"));
                try (PrintWriter pw = new PrintWriter(new File(cDir, fn))) {
                    pw.println("// " + currentProgram.getName() + "  " + f.getName()
                               + " @ 0x" + f.getEntryPoint());
                    pw.println(r.getDecompiledFunction().getC());
                }
                ok++;
            } else fail++;
        }
        di.dispose();
        println("Decompiled " + ok + " functions to " + cDir + " (" + fail + " failed)");
    }
}
