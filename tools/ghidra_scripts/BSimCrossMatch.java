// Headless BSim cross-binary function matching.
//
// Generates BSim signature vectors for a set of source functions in the current
// program and for every function in a target program, then reports the best
// structural matches. Unlike a byte-signature scan this sees through register
// allocation and compiler-version differences, which is what we need for
// DS1 (MSVC 2005) vs DS2 (MSVC 2008).
//
// Ghidra's stock LocalBSimQueryScript refuses to run headlessly and requires a
// GUI program picker; this is the same core algorithm without either.
//
// Usage (headless):
//   -process "deadspace2.exe" -noanalysis
//   -postScript BSimCrossMatch.java <targetProjectPath> <targetProgramName> \
//               <outCsv> <topN> [sourceVA...]
//
// If no source VAs are given, every function in the current program is queried.
//
//@category BSim

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

import generic.lsh.vector.LSHVector;
import generic.lsh.vector.LSHVectorFactory;
import generic.lsh.vector.VectorCompare;
import ghidra.app.script.GhidraScript;
import ghidra.base.project.GhidraProject;
import ghidra.features.bsim.query.FunctionDatabase;
import ghidra.features.bsim.query.GenSignatures;
import ghidra.features.bsim.query.client.Configuration;
import ghidra.features.bsim.query.description.FunctionDescription;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Program;

public class BSimCrossMatch extends GhidraScript {

    // Signature settings template. medium_nosize ignores function size, which
    // matters here: the same logic compiled by two MSVC versions differs in size.
    private static final String TEMPLATE_NAME = "medium_nosize";

    // Functions below this self-significance are too generic to match usefully
    // (tiny stubs, thunks) and would otherwise produce a flood of noise.
    private static final double SELF_SIGNIFICANCE_BOUND = 15.0;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 4) {
            println("ERROR: need <targetProjectPath> <targetProgramName> <outCsv> <topN> [VA...]");
            return;
        }
        String targetProjectPath = args[0];
        String targetProgramName = args[1];
        File outCsv = new File(args[2]);
        int topN = Integer.parseInt(args[3]);

        // ---- resolve the source function set ----
        FunctionManager srcMan = currentProgram.getFunctionManager();
        Set<Function> sourceFuncs = new HashSet<>();
        if (args.length > 4) {
            for (int i = 4; i < args.length; i++) {
                Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
                Function f = srcMan.getFunctionContaining(a);
                if (f == null) {
                    println("WARN: no function containing " + args[i]);
                    continue;
                }
                sourceFuncs.add(f);
            }
        }
        else {
            Iterator<Function> it = srcMan.getFunctions(true);
            while (it.hasNext()) {
                sourceFuncs.add(it.next());
            }
        }
        if (sourceFuncs.isEmpty()) {
            println("ERROR: no source functions resolved");
            return;
        }
        println("Source functions: " + sourceFuncs.size());

        // ---- open the target program read-only from its own project ----
        int slash = targetProjectPath.lastIndexOf(File.separatorChar);
        String projectDir = targetProjectPath.substring(0, slash);
        String projectName = targetProjectPath.substring(slash + 1);
        GhidraProject project = GhidraProject.openProject(projectDir, projectName, false);
        Program targetProgram = project.openProgram("/", targetProgramName, false);
        println("Target program: " + targetProgram.getName() + "  functions="
            + targetProgram.getFunctionManager().getFunctionCount());

        try {
            LSHVectorFactory factory = FunctionDatabase.generateLSHVectorFactory();
            Configuration config = FunctionDatabase.loadConfigurationTemplate(TEMPLATE_NAME);
            factory.set(config.weightfactory, config.idflookup, config.info.settings);

            GenSignatures srcSigs =
                generateSignatures(currentProgram, sourceFuncs.iterator(), sourceFuncs.size(), factory);
            FunctionManager tgtMan = targetProgram.getFunctionManager();
            GenSignatures tgtSigs = generateSignatures(targetProgram,
                tgtMan.getFunctions(true), tgtMan.getFunctionCount(), factory);
            println("Signatures generated; comparing...");

            try (PrintWriter pw = new PrintWriter(outCsv)) {
                pw.println("source_va,source_name,rank,target_va,target_name,similarity,significance");

                Iterator<FunctionDescription> srcIter =
                    srcSigs.getDescriptionManager().listAllFunctions();
                VectorCompare cmp = new VectorCompare();
                int done = 0;
                while (srcIter.hasNext()) {
                    if (monitor.isCancelled()) break;
                    FunctionDescription srcDesc = srcIter.next();
                    LSHVector srcVec = srcDesc.getSignatureRecord().getLSHVector();
                    if (factory.getSelfSignificance(srcVec) <= SELF_SIGNIFICANCE_BOUND) {
                        continue;
                    }

                    List<Match> matches = new ArrayList<>();
                    Iterator<FunctionDescription> tgtIter =
                        tgtSigs.getDescriptionManager().listAllFunctions();
                    while (tgtIter.hasNext()) {
                        FunctionDescription tgtDesc = tgtIter.next();
                        LSHVector tgtVec = tgtDesc.getSignatureRecord().getLSHVector();
                        if (factory.getSelfSignificance(tgtVec) <= SELF_SIGNIFICANCE_BOUND) {
                            continue;
                        }
                        double sim = srcVec.compare(tgtVec, cmp);
                        double sig = factory.calculateSignificance(cmp);
                        matches.add(new Match(tgtDesc.getAddress(), tgtDesc.getFunctionName(), sim, sig));
                    }
                    matches.sort((a, b) -> Double.compare(b.similarity, a.similarity));

                    for (int i = 0; i < Math.min(topN, matches.size()); i++) {
                        Match m = matches.get(i);
                        pw.printf("0x%08X,%s,%d,0x%08X,%s,%.4f,%.2f%n",
                            srcDesc.getAddress(), srcDesc.getFunctionName(), i + 1,
                            m.address, m.name, m.similarity, m.significance);
                    }
                    if (!matches.isEmpty()) {
                        Match best = matches.get(0);
                        println(String.format("0x%08X %-22s -> 0x%08X %-22s sim=%.3f sig=%.1f",
                            srcDesc.getAddress(), srcDesc.getFunctionName(),
                            best.address, best.name, best.similarity, best.significance));
                    }
                    done++;
                }
                println("Compared " + done + " source functions -> " + outCsv);
            }
        }
        finally {
            // The GhidraProject owns the consumer reference for programs it opens,
            // so closing the project releases it. Calling release(this) here throws
            // IllegalArgumentException("unknown consumer").
            project.close();
        }
    }

    private GenSignatures generateSignatures(Program program, Iterator<Function> funcs,
            int count, LSHVectorFactory factory) throws Exception {
        GenSignatures gensig = new GenSignatures(false);
        gensig.setVectorFactory(factory);
        gensig.openProgram(program, null, null, null, null, null);
        gensig.scanFunctions(funcs, count, monitor);
        return gensig;
    }

    private static class Match {
        final long address;
        final String name;
        final double similarity;
        final double significance;

        Match(long address, String name, double similarity, double significance) {
            this.address = address;
            this.name = name;
            this.similarity = similarity;
            this.significance = significance;
        }
    }
}
