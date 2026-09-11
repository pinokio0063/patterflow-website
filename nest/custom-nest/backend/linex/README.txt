PatternFlow custom nest — Linux
================================
Same C++ engine as custom nesting/engine. Rules/logic are 100% the same.
Do not edit pf_nest.cpp or the .hpp files in this folder.

Already built (this PC):  nest
  Type: Linux x86_64 ELF  (not Windows .exe)
  I/O:  stdin JSON → stdout JSON   (same as nest.exe)

On a Linux server:
  chmod +x nest
  ./nest < input.json > result.json

Rebuild on Linux (Ubuntu/Debian):
  sudo apt-get update
  sudo apt-get install -y g++ make
  chmod +x build.sh
  ./build.sh

Rebuild from Windows (cross-compile, stays in this folder):
  node cross-build.js

Windows nest.exe stays in ../engine. Linux files stay in this folder only.
