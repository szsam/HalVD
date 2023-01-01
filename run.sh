#!/usr/bin/env bash

LLVM_DIR="/usr/lib/llvm-14"

#BITCODES=$(find "$RTOSExploration/bitcode-db/Amazfitbip" -name "*.bc")
BITCODES=$(find "$RTOSExploration/bitcode-db/" -name "*.bc")

parallel -i sh -c "${LLVM_DIR}/bin/opt \
  -load-pass-plugin build/lib/libFindMMIOFunc.so \
  -load-pass-plugin build/lib/libFindHALBypass.so \
  --passes='print<hal-bypass>' --disable-output {} 2> {}.analysis" -- $BITCODES

