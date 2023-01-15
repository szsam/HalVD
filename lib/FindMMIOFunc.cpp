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

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <regex>

using namespace llvm;

// Pretty-prints the result of this analysis
static void printMMIOFuncResult(llvm::raw_ostream &OutS,
                                const FindMMIOFunc::Result &);

//------------------------------------------------------------------------------
// FindMMIOFunc Implementation
//------------------------------------------------------------------------------
// InstTy = LoadInst or StoreInst
template <typename InstTy>
bool FindMMIOFunc::isMMIOInst_(llvm::Instruction *Ins) {
  auto *TheIns = dyn_cast<InstTy>(Ins);
  if (!TheIns)
    return false;
  auto *CE = dyn_cast<ConstantExpr>(TheIns->getPointerOperand());
  if (!(CE && CE->getOpcode() == Instruction::IntToPtr))
    return false;

  MY_DEBUG(dbgs() << *Ins << "\n");
  const APInt &Addr = cast<ConstantInt>(CE->getOperand(0))->getValue();
  SmallVector<char> Str;
  Addr.toStringUnsigned(Str, 16);
  MY_DEBUG(dbgs() << "Addr: 0x" << Str << "\n");

  const DebugLoc &Debug = Ins->getDebugLoc();
  if (Debug) {
    MY_DEBUG(dbgs() << *Debug << "\n");
  }

  return true;
}

bool FindMMIOFunc::isMMIOInst(llvm::Instruction *Ins) {
  return (isMMIOInst_<LoadInst>(Ins) || isMMIOInst_<StoreInst>(Ins) ||
          isMMIOInst_<GetElementPtrInst>(Ins));
}

void FindMMIOFunc::findMMIOFunc(Module &M, Result &MMIOFuncs) {
  for (auto &Func : M) {
    if (ignoreFunc(Func))
      continue;
    for (auto &Ins : instructions(Func)) {
      if (!isMMIOInst(&Ins))
        continue;
      if (Ins.getDebugLoc() && Ins.getDebugLoc().getInlinedAt())
        continue;
      MY_DEBUG(dbgs() << "MMIO func: " << Func.getName() << "\n");
      // MMIOFuncs[&Func] = MMIOFunc(&Ins);
      MMIOFuncs.insert({&Func, MMIOFunc(&Ins)});
      break;
    }
  }
}

// Ugly workaround to filter out functions that call macro HAL functions
bool FindMMIOFunc::ignoreFunc(llvm::Function &F) {
  DISubprogram *DISub = F.getSubprogram();
  if (!DISub) return false;
  DIFile *File = DISub->getFile();
  std::string FullPath = std::string(File->getDirectory()) + "/"
                         + std::string(File->getFilename());
  std::regex PathRe("freertos.*(queue|tasks|timers)\\.c", std::regex::icase);
  if (std::regex_search(FullPath, PathRe))
    return true;
  std::regex FuncRe("Pinetime.*PushMessage|nrfx_gpiote_evt_handler");
  if (F.hasName() && std::regex_search(std::string(F.getName()), FuncRe))
    return true;
  return false;
}

FindMMIOFunc::Result FindMMIOFunc::runOnModule(Module &M) {
  Result Res;
  findMMIOFunc(M, Res);
  return Res;
}

PreservedAnalyses FindMMIOFuncPrinter::run(Module &M,
                                           ModuleAnalysisManager &MAM) {

  auto &MMIOFuncs = MAM.getResult<FindMMIOFunc>(M);

  printMMIOFuncResult(OS, MMIOFuncs);
  return PreservedAnalyses::all();
}

FindMMIOFunc::Result FindMMIOFunc::run(llvm::Module &M,
                                       llvm::ModuleAnalysisManager &) {
  return runOnModule(M);
}

// bool LegacyFindMMIOFunc::runOnModule(llvm::Module &M) {
//  DirectCalls = Impl.runOnModule(M);
//  return false;
//}
//
// void LegacyFindMMIOFunc::print(raw_ostream &OutS, Module const *) const {
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
// char LegacyFindMMIOFunc::ID = 0;
//
//// #1 REGISTRATION FOR "opt -analyze -legacy-static-cc"
// RegisterPass<LegacyFindMMIOFunc>
//    X(/*PassArg=*/"legacy-static-cc",
//      /*Name=*/"LegacyFindMMIOFunc",
//      /*CFGOnly=*/true,
//      /*is_analysis=*/true);

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------
static void printMMIOFuncResult(raw_ostream &OutS,
                                const FindMMIOFunc::Result &Res) {
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
  OutS << "MMIO-func(location of mmio inst)\n";
  for (auto &Node : Res) {
    OutS << Node.first->getName() << " ";
    // DISubprogram *DISub = F.Func->getSubprogram();
    // if (DISub && DISub->getFile())
    //  OutS << " " << DISub->getFile()->getFilename();
    Node.second.MMIOIns->getDebugLoc().print(OutS);
    OutS << "\n";
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}
/* vim: set ts=2 sts=2 sw=2: */
