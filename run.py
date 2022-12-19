import subprocess
import re

firmware_list = []

LLVM_DIR='/usr/lib/llvm-14'

with open("firmware.txt") as f:
    for line in f:
        bc = line.strip()
        if bc and not bc.startswith('#'):
            name = re.sub(r"\.[^.]+\.(bc|ll)$", "", bc)  # remove ".xxx.(bc|ll)"
            name = re.sub(r"(purs3|_?build)", "", name)
            name = re.sub(r"[^\w\s-]", "-", name)
            name = re.sub(r"[-\s]+", "-", name).strip("-_")
            firmware_list.append((name, bc))

subprocess.check_output("mkdir -p results", shell=True)

for name, bc in firmware_list:
    subprocess.run(f"{LLVM_DIR}/bin/opt -load-pass-plugin build/lib/libFindMMIOFunc.so -load-pass-plugin build/lib/libFindHALBypass.so --passes='print<hal-bypass>' --disable-output {bc} 2> results/{name}.out", shell=True)
