#!/usr/bin/env bash

LLVM_DIR="/usr/lib/llvm-14"

#BITCODES=$(find "$RTOSExploration/bitcode-db/zephyr-samples/96b_stm32_sensor_mez" -name "*.bc")
BITCODES=$(find "$RTOSExploration/bitcode-db/InfiniTime" -name "*.bc")

parallel -i sh -c "${LLVM_DIR}/bin/opt \
  -load-pass-plugin build/lib/libFindMMIOFunc.so \
  -load-pass-plugin build/lib/libFindHALBypass.so \
  --passes='print<hal-bypass>' --disable-output {} 2> {}.analysis" -- $BITCODES

