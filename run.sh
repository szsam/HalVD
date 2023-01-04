#!/usr/bin/env bash

LLVM_DIR="/usr/lib/llvm-14"

#BITCODES=$(find "$RTOSExploration/bitcode-db/Amazfitbip" -name "*.bc")
BITCODES=$(find "$RTOSExploration/bitcode-db/" \
  -not \( -path "$RTOSExploration/bitcode-db/esp-idf-examples" -prune \) -name "*.bc")
BITCODES_ESP_IDF=$(find "$RTOSExploration/bitcode-db/esp-idf-examples" -name "*.ll")
BITCODES="$BITCODES $BITCODES_ESP_IDF"

parallel -i sh -c "${LLVM_DIR}/bin/opt \
  -load-pass-plugin build/lib/libFindMMIOFunc.so \
  -load-pass-plugin build/lib/libFindHALBypass.so \
  --passes='print<hal-bypass>' --disable-output {} 2> {}.analysis" -- $BITCODES

