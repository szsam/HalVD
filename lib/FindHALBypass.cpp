//==============================================================================
// FILE:
//    FindHALBypass.cpp
//
// DESCRIPTION:
//    Counts the number of static function calls in the input module. `Static`
//    refers to the fact that the analysed functions calls are compile-time
//    calls (as opposed to `dynamic`, i.e. run-time). Only direct function
//    calls are considered. Calls via functions pointers are not taken into
//    account.
//
//    This pass is used in `static`, a tool implemented in tools/StaticMain.cpp
//    that is a wrapper around FindHALBypass. `static` allows you to run
//    FindHALBypass without `opt`.
//
// USAGE:
//    1. Legacy PM
//      opt -load libFindHALBypass.dylib -legacy-static-cc `\`
//        -analyze <input-llvm-file>
//    2. New PM
//      opt -load-pass-plugin libFindHALBypass.dylib `\`
//        -passes="print<static-cc>" `\`
//        -disable-output <input-llvm-file>
//
// License: MIT
//==============================================================================
#include "FindHALBypass.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <algorithm>

using namespace llvm;

// Pretty-prints the result of this analysis
static void printHALBypassResult(llvm::raw_ostream &OutS,
                                 const FindHALBypass::Result &);

//------------------------------------------------------------------------------
// FindHALBypass Implementation
//------------------------------------------------------------------------------
FindHALBypass::Result
FindHALBypass::runOnModule(Module &M, const FindMMIOFunc::Result &MMIOFuncs) {
  Result Res;
  CallGraph CG = CallGraph(M);
  CG.dump();

  for (auto &F : CG) {
    std::string CallerName("NONAME");
    if (F.first && F.first->hasName()) {
      CallerName = F.first->getName().str();
    }
    // dbgs() << "HAL: " << CallerName << "\n";
    // continue if not starting with "_Z"
    //if (CallerName.rfind("_ZN8Pinetime", 0) != 0)
    //  continue;

    for (auto &CR : *F.second) {
      auto Callee = CR.second->getFunction();

      std::string CalleeName("NONAME");
      if (Callee && Callee->hasName())
        CalleeName = Callee->getName().str();

      auto It = MMIOFuncs.find(Callee);
      //auto It = std::find_if(MMIOFuncs.begin(), MMIOFuncs.end(),
      //                       [Callee](const FindMMIOFunc::NonHalMMIOFunc &F) {
      //                         return F.Func == Callee;
      //                       });
      if (It != MMIOFuncs.end()) {
        dbgs() << "HAL bypass: " << CallerName << " -> " << CalleeName << "\n";
      }
    }
  }

  // for (auto &f: MMIOFuncs)
  //     dbgs() << f->getName() << "   HAL\n";

  return Res;
}

PreservedAnalyses FindHALBypassPrinter::run(Module &M,
                                            ModuleAnalysisManager &MAM) {

  auto &Res = MAM.getResult<FindHALBypass>(M);

  printHALBypassResult(OS, Res);
  return PreservedAnalyses::all();
}

FindHALBypass::Result FindHALBypass::run(llvm::Module &M,
                                         llvm::ModuleAnalysisManager &MAM) {
  auto &Funcs = MAM.getResult<FindMMIOFunc>(M);
  return runOnModule(M, Funcs);
}

// bool LegacyFindHALBypass::runOnModule(llvm::Module &M) {
//  DirectCalls = Impl.runOnModule(M);
//  return false;
//}
//
// void LegacyFindHALBypass::print(raw_ostream &OutS, Module const *) const {
//  printStaticCCResult(OutS, DirectCalls);
//}

//------------------------------------------------------------------------------
// New PM Registration
//------------------------------------------------------------------------------
AnalysisKey FindHALBypass::Key;

llvm::PassPluginLibraryInfo getFindHALBypassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "hal-bypass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // #1 REGISTRATION FOR "opt -passes=print<hal-bypass>"
            PB.registerPipelineParsingCallback(
                [&](StringRef Name, ModulePassManager &MPM,
                    ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "print<hal-bypass>") {
                    MPM.addPass(FindHALBypassPrinter(llvm::errs()));
                    return true;
                  }
                  return false;
                });
            // #2 REGISTRATION FOR "MAM.getResult<FindHALBypass>(Module)"
            PB.registerAnalysisRegistrationCallback(
                [](ModuleAnalysisManager &MAM) {
                  MAM.registerPass([&] { return FindHALBypass(); });
                });
          }};
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getFindHALBypassPluginInfo();
}

//------------------------------------------------------------------------------
// Legacy PM Registration
//------------------------------------------------------------------------------
// char LegacyFindHALBypass::ID = 0;
//
//// #1 REGISTRATION FOR "opt -analyze -legacy-static-cc"
// RegisterPass<LegacyFindHALBypass>
//    X(/*PassArg=*/"legacy-static-cc",
//      /*Name=*/"LegacyFindHALBypass",
//      /*CFGOnly=*/true,
//      /*is_analysis=*/true);

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------
static void printHALBypassResult(raw_ostream &OutS,
                                 const FindHALBypass::Result &MMIOFunc) {
  OutS << "================================================="
       << "\n";
  OutS << "LLVM-TUTOR: HAL bypass\n";
  //  const char *str1 = "NAME";
  //  const char *str2 = "#N DIRECT CALLS";
  //  OutS << format("%-20s %-10s\n", str1, str2);
  //  OutS << "-------------------------------------------------"
  //       << "\n";
  //
  //  for (auto &Func: MMIOFunc) {
  //    OutS << Func->getName() << "\n";
  //    // OutS << format("%-20s %-10lu\n",
  //    CallCount.first->getName().str().c_str(),
  //    //                CallCount.second);
  //  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}
