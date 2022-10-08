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
bool FindHALBypass::isHal(const std::string &Str) {
  return (Str.find("hal") != std::string::npos &&
          Str.find("halt") == std::string::npos) ||
         Str.find("driver") != std::string::npos ||
         Str.find("cmsis") != std::string::npos;
         //Str.find("port") != std::string::npos;
}

bool FindHALBypass::isHalFunc(const llvm::Function &F) {
  DISubprogram *DISub = F.getSubprogram();
  if (!DISub) {
    errs() << "Warning: isHalFunc: DISubprogram not exists.\n";
    return false;
  }
  DIFile *File = DISub->getFile();
  MY_DEBUG(DISub->dump());
  MY_DEBUG(File->dump());

  std::string Name(DISub->getName());
  std::string LinkageName(DISub->getLinkageName());
  std::string Filename(File->getFilename());
  if (isHal(Name) || isHal(LinkageName) || isHal(Filename)) {
    MY_DEBUG(dbgs() << "Hal function: " << DISub->getName() << " "
                    << LinkageName << " " << Filename << "\n");
    return true;
  }
  return false;
}

bool FindHALBypass::isLibFunc(const llvm::Function &F) {
  DISubprogram *DISub = F.getSubprogram();
  if (!DISub || !DISub->getFile()) {
    errs() << "Warning: isLibFunc: DISubprogram not exists.\n";
    return false;
  }
  std::string Filename(DISub->getFile()->getFilename());
  return (Filename.find("SDK") != std::string::npos) ||
         (Filename.find("lib") != std::string::npos) ||
         (Filename.find("driver") != std::string::npos);
}

FindHALBypass::Result
FindHALBypass::runOnModule(Module &M, const FindMMIOFunc::Result &MMIOFuncs) {
  Result Res;
  for (auto &Node : MMIOFuncs) {
    const Function *F = Node.first;
    MMIOFunc MF = MMIOFunc(Node.second);
    if (isLibFunc(*F)) {
      MF.IsLib = true;
      MF.IsHal = isHalFunc(*F);
    }
    Res.insert({F, MF});
  }

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
                                 const FindHALBypass::Result &MMIOFuncs) {
  OutS << "================================================="
       << "\n";
  OutS << "LLVM-TUTOR: HAL bypass\n";
  //  const char *str1 = "NAME";
  //  const char *str2 = "#N DIRECT CALLS";
  //  OutS << format("%-20s %-10s\n", str1, str2);
  //  OutS << "-------------------------------------------------"
  //       << "\n";
  //
  for (auto &Node : MMIOFuncs) {
    if (!Node.second.IsLib) {
      OutS << Node.first->getName() << " ";
      Node.second.MMIOIns->getDebugLoc().print(OutS);
      OutS << "\n";
    }
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}
