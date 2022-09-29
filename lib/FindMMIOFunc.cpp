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
#include "llvm/Analysis/CallGraph.h"

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

  dbgs() << *Ins << "\n";
  const APInt &Addr = cast<ConstantInt>(CE->getOperand(0))->getValue();
  SmallVector<char> Str;
  Addr.toStringUnsigned(Str, 16);
  dbgs() << "Addr: " << Str << "\n";

  const DebugLoc &Debug = Ins->getDebugLoc();
  if (Debug) {
    dbgs() << *Debug << "\n";
    //Debug.dump();
  }

  return true;
}

bool FindMMIOFunc::isMMIOInst(llvm::Instruction *Ins) {
  return (isMMIOInst_<LoadInst>(Ins) || isMMIOInst_<StoreInst>(Ins) ||
          isMMIOInst_<GetElementPtrInst>(Ins));
}

bool FindMMIOFunc::containHalStr(const std::string &Str) {
  return (Str.find("hal") != std::string::npos &&
          Str.find("halt") == std::string::npos);
}

bool FindMMIOFunc::isHalFunc(const llvm::Function &F) {
  DISubprogram *DISub = F.getSubprogram();
  if (!DISub) {
    dbgs() << "No debug info for this func\n";
    return false;
  }
  DISub->dump();
  DIFile *File = DISub->getFile();
  File->dump();

  std::string Name(DISub->getName());
  std::string LinkageName(DISub->getLinkageName());
  std::string Filename(File->getFilename());
  if (containHalStr(Name) || containHalStr(LinkageName) ||
      containHalStr(Filename)) {
    dbgs() << "Hal function: " << DISub->getName() << " "
      << LinkageName << " " << Filename << "\n";
    return true;
  }
  return false;
}

bool FindMMIOFunc::isAppFunc(const llvm::Function &F) {
  // return true if F MAY be an application function
  DISubprogram *DISub = F.getSubprogram();
  if (!DISub || !DISub->getFile())
    return true;
  std::string Filename(DISub->getFile()->getFilename());
  if (Filename.find("SDK") != std::string::npos)
    return false;
  if (Filename.find("lib") != std::string::npos)
    return false;
  return true;
}

void FindMMIOFunc::findNonHalMMIOFunc(Module &M, Result &MMIOFuncs) {
  for (auto &Func : M) {
    if (isHalFunc(Func))
      goto CheckNextFunction;
    for (auto &Ins: instructions(Func)) {
      if (isMMIOInst(&Ins)) {
        dbgs() << "Non-hal MMIO func: " << Func.getName() << "\n";
        //MMIOFuncs[&Func] = NonHalMMIOFunc(&Ins);
        MMIOFuncs.insert({&Func, NonHalMMIOFunc(&Ins)});
        goto CheckNextFunction;
      }
    }
CheckNextFunction:
    dbgs() << "\n";
    continue;
  }
}

void FindMMIOFunc::checkCalledByApp(Module &M, Result &MMIOFuncs) {
  CallGraph CG = CallGraph(M);
  CG.dump();
  for (auto &I : CG) {
    const Function *Caller = I.first;
    if (Caller && !isAppFunc(*Caller))
      continue;
    for (auto &J : *I.second) {
      //if (J.first) {
      //  auto *Inst = (Instruction *)(Value *)J.first.getValue();
      //  Inst->dump();
      //}
      const Function *Callee = J.second->getFunction();
      auto Iter = MMIOFuncs.find(Callee);
      if (Iter != MMIOFuncs.end()) {
        Iter->second.CalledByApp = true;
        Iter->second.Caller = Caller;
      }
    }
  }
}

FindMMIOFunc::Result FindMMIOFunc::runOnModule(Module &M) {
  Result Res;
  findNonHalMMIOFunc(M, Res);
  checkCalledByApp(M, Res);
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
  OutS << "LLVM-TUTOR: Non-hal MMIO functions\n";
  OutS << "=================================================\n";
  //  const char *str1 = "NAME";
  //  const char *str2 = "#N DIRECT CALLS";
  //  OutS << format("%-20s %-10s\n", str1, str2);
  //  OutS << "-------------------------------------------------"
  //       << "\n";
  //
  for (auto &KV : Res) {
    if (!KV.second.CalledByApp)
      continue;
    OutS << KV.first->getName();
    //DISubprogram *DISub = F.Func->getSubprogram();
    //if (DISub && DISub->getFile())
    //  OutS << " " << DISub->getFile()->getFilename();
    const DebugLoc &DebugLoc = KV.second.MMIOIns->getDebugLoc();
    if (DebugLoc)
      OutS << "(" << cast<DIScope>(DebugLoc.getScope())->getFilename()
           << ":" << DebugLoc.getLine() << ":" << DebugLoc.getCol() << ")";
    OutS << " called by ";
    if (KV.second.Caller) {
      OutS << KV.second.Caller->getName();
      DISubprogram *DI = KV.second.Caller->getSubprogram();
      if (DI && DI->getFile())
        OutS << "(" << DI->getFile()->getFilename() << ")";
    }
    else
      OutS << "external node";
    OutS << "\n";
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}
/* vim: set ts=2 sts=2 sw=2: */
