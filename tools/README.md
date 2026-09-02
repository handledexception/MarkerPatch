# MarkerPatch analysis tools

Scripts used to analyse the Dead Space 1 and Dead Space 2 binaries while
planning the DS1 port. They reproduce every table in [`PLAN.md`](../PLAN.md).

Everything here is CLI-only — no GUI reverse-engineering tooling is required.

## Setup

```
pip install pefile capstone
```

Game paths default to a typical install. Override them with environment
variables or per-script flags:

```
set MARKERPATCH_DS1=D:\GOG\Dead Space\Dead Space.exe
set MARKERPATCH_DS2=D:\SteamLibrary\steamapps\common\Dead Space 2\deadspace2.exe
```

Run the Python scripts from this directory so `dsbin` is importable.

## Scripts

| Script | Purpose |
|---|---|
| `dsbin.py` | Shared library: signature parsing, PE offset/VA translation, disassembly normalisation, similarity scoring |
| `sigscan.py` | Scan every `ScanModuleSignature` pattern in `src/` against both binaries |
| `classify.py` | Verify cross-matches by disassembling both sites and scoring structural similarity |
| `xref.py` | Resolve a string to its containing function by immediate-value xref |
| `table.py` | Per-feature port matrix; also emits `sigtable.json` |
| `ghidra_scripts/ExportNeighborhood.java` | Ghidra headless post-script: function inventory CSV + decompiled call-graph neighbourhood around anchor addresses |

## Typical workflow

```
python sigscan.py                 # which signatures hit both binaries?
python classify.py                # which of those hits are real?
python classify.py --disasm --only InitializePhysicsSolverParameters
python table.py                   # regenerate the PLAN.md port matrix
```

## Ghidra

Import and analyse a binary headlessly:

```
set JAVA_HOME=C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot
C:\Tools\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat ^
    C:\Tools\ghidra_projects DeadSpace ^
    -import "D:\GOG\Dead Space\Dead Space.exe" ^
    -processor x86:LE:32:default -max-cpu 8
```

Then export a function inventory plus the decompiled neighbourhood around one
or more anchor addresses (depth 2 here):

```
C:\Tools\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat ^
    C:\Tools\ghidra_projects DeadSpace ^
    -process "Dead Space.exe" -noanalysis ^
    -scriptPath %CD%\ghidra_scripts ^
    -postScript ExportNeighborhood.java C:\some\outdir 2 0x00846500
```

The resulting `*_functions.csv` is what `xref.py --csv` consumes.

## Caveat

`sigscan.py` only finds **byte-identical** code. A "no match" is not evidence
that a function is absent from the other game — only that its codegen differs
(DS1 is MSVC 2005, DS2 is MSVC 2008). Use Ghidra BSim for structural matching.

Equally, a raw pattern hit is not proof of a match. Short patterns starting with
a generic MSVC prologue collide with unrelated functions that share a stack
frame size. Always confirm with `classify.py` before trusting a result.
