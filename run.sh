#!/usr/bin/env bash

LLVM_DIR="/usr/lib/llvm-14"

#BITCODES=$(find "$RTOSExploration"/bitcode-db/{mbed-os,nuttx,phoenix-rtos,zephyr-samples,InfiniTime} -name "*.bc")
BITCODES=$(find "$RTOSExploration/bitcode-db/" \
  -not \( -path "$RTOSExploration/bitcode-db/esp-idf-examples" -prune \) -name "*.bc")
BITCODES_ESP_IDF=$(find "$RTOSExploration/bitcode-db/esp-idf-examples" -name "*.ll")
BITCODES="$BITCODES $BITCODES_ESP_IDF"

parallel -i sh -c "${LLVM_DIR}/bin/opt \
  -load-pass-plugin build/lib/libFindMMIOFunc.so \
  -load-pass-plugin build/lib/libFindHALBypass.so \
  --passes='print<hal-bypass>' --disable-output {} 2> {}.analysis" -- $BITCODES

#find "$RTOSExploration/bitcode-db/" -name "*.analysis" \
#  -exec bash -c "echo -ne '{} '; tail -n 1 '{}'; echo" \; \
#  > "$RTOSExploration/bitcode-db/summary.txt"

#find . -name "*.analysis" | xargs grep 'MMIO_F:' --no-filename | sed 's/MMIO_F: //g' | sort -u -k1,1 | sort -n -k4,4 -k5,5 > dataset.txt
#awk '{print $4,$5}' dataset.txt  | uniq -c > statistics.txt
