//==============================================================================
// FILE:
//    FindMMIOFunc.cpp
//
// DESCRIPTION:
//    Counts the number of static function calls in the input module. `Static`
//    refers to the fact that the analysed functions calls are compile-time
//    calls (as opposed to `dynamic`, i.e. run-time). Only direct function
//    calls are considered. Calls via functions pointers are not taken into
//    account.
//
//    This pass is used in `static`, a tool implemented in tools/StaticMain.cpp
//    that is a wrapper around FindMMIOFunc. `static` allows you to run
//    FindMMIOFunc without `opt`.
//
// USAGE:
//    1. Legacy PM
//      opt -load libFindMMIOFunc.dylib -legacy-static-cc `\`
//        -analyze <input-llvm-file>
//    2. New PM
//      opt -load-pass-plugin libFindMMIOFunc.dylib `\`
//        -passes="print<static-cc>" `\`
//        -disable-output <input-llvm-file>
//
// License: MIT
//==============================================================================
#include "FindMMIOFunc.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

// Pretty-prints the result of this analysis
static void printMMIOFuncResult(llvm::raw_ostream &OutS,
                         const FindMMIOFunc::Result &);

//------------------------------------------------------------------------------
// FindMMIOFunc Implementation
//------------------------------------------------------------------------------
// InstTy = LoadInst or StoreInst
template <typename InstTy>
bool FindMMIOFunc::isMMIOInst(llvm::Instruction *Ins) {
    if (auto CastedIns = dyn_cast<InstTy>(Ins)) {
        // if (CastedIns->isVolatile()) {
        // }
        auto CE = dyn_cast<ConstantExpr>(CastedIns->getPointerOperand());
        if (CE && CE->getOpcode() == Instruction::IntToPtr) {
            dbgs() << *Ins << "\n";
            return true;
        }
    }
    return false;
}

FindMMIOFunc::Result FindMMIOFunc::runOnModule(Module &M) {
  Result Res;

  for (auto &Func : M) {
    for (auto &BB : Func) {
      for (auto &Ins : BB) {

        if (isMMIOInst<LoadInst>(&Ins) || isMMIOInst<StoreInst>(&Ins)
                || isMMIOInst<GetElementPtrInst>(&Ins)) {
            dbgs() << "MMIO func: " << Func.getName() << "\n";
            Res.push_back(&Func);
            goto CheckNextFunction;
        }



        // // If CB is a direct function call then DirectInvoc will be not null.
        // auto DirectInvoc = CB->getCalledFunction();
        // if (nullptr == DirectInvoc) {
        //   continue;
        // }

        // // We have a direct function call - update the count for the function
        // // being called.
        // auto CallCount = Res.find(DirectInvoc);
        // if (Res.end() == CallCount) {
        //   CallCount = Res.insert(std::make_pair(DirectInvoc, 0)).first;
        // }
        // ++CallCount->second;
      }
    }
CheckNextFunction:
    ;
  }

  return Res;
}

PreservedAnalyses
FindMMIOFuncPrinter::run(Module &M,
                              ModuleAnalysisManager &MAM) {

  auto &MMIOFuncs = MAM.getResult<FindMMIOFunc>(M);

  printMMIOFuncResult(OS, MMIOFuncs);
  return PreservedAnalyses::all();
}

FindMMIOFunc::Result
FindMMIOFunc::run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
  return runOnModule(M);
}

//bool LegacyFindMMIOFunc::runOnModule(llvm::Module &M) {
//  DirectCalls = Impl.runOnModule(M);
//  return false;
//}
//
//void LegacyFindMMIOFunc::print(raw_ostream &OutS, Module const *) const {
//  printStaticCCResult(OutS, DirectCalls);
//}

//------------------------------------------------------------------------------
// New PM Registration
//------------------------------------------------------------------------------
AnalysisKey FindMMIOFunc::Key;

llvm::PassPluginLibraryInfo getFindMMIOFuncPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "mmio-func", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // #1 REGISTRATION FOR "opt -passes=print<mmio-func>"
            PB.registerPipelineParsingCallback(
                [&](StringRef Name, ModulePassManager &MPM,
                    ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "print<mmio-func>") {
                    MPM.addPass(FindMMIOFuncPrinter(llvm::errs()));
                    return true;
                  }
                  return false;
                });
            // #2 REGISTRATION FOR "MAM.getResult<FindMMIOFunc>(Module)"
            PB.registerAnalysisRegistrationCallback(
                [](ModuleAnalysisManager &MAM) {
                  MAM.registerPass([&] { return FindMMIOFunc(); });
                });
          }};
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getFindMMIOFuncPluginInfo();
}

//------------------------------------------------------------------------------
// Legacy PM Registration
//------------------------------------------------------------------------------
//char LegacyFindMMIOFunc::ID = 0;
//
//// #1 REGISTRATION FOR "opt -analyze -legacy-static-cc"
//RegisterPass<LegacyFindMMIOFunc>
//    X(/*PassArg=*/"legacy-static-cc",
//      /*Name=*/"LegacyFindMMIOFunc",
//      /*CFGOnly=*/true,
//      /*is_analysis=*/true);

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------
static void printMMIOFuncResult(raw_ostream &OutS,
                                const FindMMIOFunc::Result &MMIOFunc) {
  OutS << "================================================="
       << "\n";
  OutS << "LLVM-TUTOR: MMIO functions\n";
  OutS << "=================================================\n";
//  const char *str1 = "NAME";
//  const char *str2 = "#N DIRECT CALLS";
//  OutS << format("%-20s %-10s\n", str1, str2);
//  OutS << "-------------------------------------------------"
//       << "\n";
//
  for (auto &Func: MMIOFunc) {
    OutS << Func->getName() << "\n";
    // OutS << format("%-20s %-10lu\n", CallCount.first->getName().str().c_str(),
    //                CallCount.second);
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}
