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
  MMIOFuncMap.clear();
  for (auto &Node : MMIOFuncs) {
    const Function *F = Node.first;
    MMIOFunc MF = MMIOFunc(Node.second);
    MF.IsHal = isHalFunc(*F);
    MMIOFuncMap.insert({F, MF});
  }
  CallGraph CG = CallGraph(M);
  callGraphBasedHalIdent(CG);

  return MMIOFuncMap;
}

bool FindHALBypass::isHalFunc(const llvm::Function &F) {
  return isHalFuncRegex(F);
}

bool FindHALBypass::isHalRegexInternal(const std::string &Name) {
  // "hal(?!t)|driver|cmsis|arch|soc"
  std::string Str(Name);
  std::transform(Str.begin(), Str.end(), Str.begin(),
      [](unsigned char c){ return std::tolower(c); });
  return (Str.find("hal") != std::string::npos &&
          Str.find("halt") == std::string::npos) ||
         Str.find("driver") != std::string::npos ||
         Str.find("arch") != std::string::npos ||
         Str.find("soc") != std::string::npos ||
         Str.find("cmsis") != std::string::npos;
         //Str.find("port") != std::string::npos;
}

bool FindHALBypass::isHalFuncRegex(const llvm::Function &F) {
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
  std::string Dir(File->getDirectory());
  if (isHalRegexInternal(Name) || isHalRegexInternal(LinkageName)
      || isHalRegexInternal(Filename) || isHalRegexInternal(Dir)) {
    MY_DEBUG(dbgs() << "Hal function: " << DISub->getName() << " "
                    << LinkageName << " " << Filename << "\n");
    return true;
  }
  return false;
}

void FindHALBypass::callGraphBasedHalIdent(llvm::CallGraph &CG) {
  for (auto &I : MMIOFuncMap) {
    I.second.InDegree = 0;
  }

  for (auto &I : CG) {
    //const Function *Caller = I.first;
    for (auto &J : *I.second) {
      const Function *Callee = J.second->getFunction();
      //auto *CI = cast<CallInst>(static_cast<Value *>(J.first.getValue()));
      //MMIOFuncMap[Callee].InDegree++;
      auto Iter = MMIOFuncMap.find(Callee);
      if (Iter != MMIOFuncMap.end()) {
        Iter->second.InDegree++;
      }
    }
  }
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

#if 0
 bool LegacyFindHALBypass::runOnModule(llvm::Module &M) {
  DirectCalls = Impl.runOnModule(M);
  return false;
}

 void LegacyFindHALBypass::print(raw_ostream &OutS, Module const *) const {
  printStaticCCResult(OutS, DirectCalls);
}
#endif

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
#if 0
 char LegacyFindHALBypass::ID = 0;

// #1 REGISTRATION FOR "opt -analyze -legacy-static-cc"
 RegisterPass<LegacyFindHALBypass>
    X(/*PassArg=*/"legacy-static-cc",
      /*Name=*/"LegacyFindHALBypass",
      /*CFGOnly=*/true,
      /*is_analysis=*/true);
#endif

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------
static void printDebugLoc(raw_ostream &OS, const DebugLoc &DL) {
  if (!DL)
    return;

   // Print source line info.
   auto *Scope = cast<DIScope>(DL.getScope());
   OS << Scope->getDirectory();
   OS << "/";
   OS << Scope->getFilename();
   OS << ':' << DL.getLine();
   if (DL.getCol() != 0)
     OS << ':' << DL.getCol();

   if (DebugLoc InlinedAtDL = DL.getInlinedAt()) {
     OS << " @[ ";
     InlinedAtDL.print(OS);
     OS << " ]";
   }
}

static void printFuncs(raw_ostream &OutS,
                       const FindHALBypass::Result &MMIOFuncs,
                       const char *Str) {
  OutS << "================================================="
       << "\n";
  OutS << "LLVM-TUTOR: " << Str << " (# = " << MMIOFuncs.size() << ")\n";
  OutS << "Function, Location of MMIO inst\n";
  OutS << "-------------------------------------------------"
       << "\n";

  for (auto &Node : MMIOFuncs) {
    OutS << Node.first->getName() << " ";
    printDebugLoc(OutS, Node.second.MMIOIns->getDebugLoc());
    OutS << " " << Node.second.InDegree;
    OutS << "\n";
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}

static void printHALBypassResult(raw_ostream &OutS,
                                 const FindHALBypass::Result &MMIOFuncs) {
  FindHALBypass::Result AppFuncs;
  FindHALBypass::Result LibHalFuncs;
  for (auto &Node : MMIOFuncs) {
    if (!Node.second.IsHal)
      AppFuncs.insert(Node);
    else
      LibHalFuncs.insert(Node);
  }
  printFuncs(OutS, AppFuncs,       "Application MMIO functions");
  printFuncs(OutS, LibHalFuncs,    "Hal MMIO functions");
}
