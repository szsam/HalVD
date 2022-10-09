//========================================================================
// FILE:
//    FindHALBypass.h
//
// DESCRIPTION:
//    Declares the FindHALBypass Passes
//      * new pass manager interface
//      * legacy pass manager interface
//      * printer pass for the new pass manager
//
// License: MIT
//========================================================================
#ifndef LLVM_TUTOR_FINDHALBYPASS_H_H
#define LLVM_TUTOR_FINDHALBYPASS_H_H

#include "FindMMIOFunc.h"

//#include "llvm/ADT/MapVector.h"
#include "llvm/IR/AbstractCallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

//------------------------------------------------------------------------------
// New PM interface
//------------------------------------------------------------------------------
//using ResultStaticCC = llvm::MapVector<const llvm::Function *, unsigned>;

struct FindHALBypass : public llvm::AnalysisInfoMixin<FindHALBypass> {
  struct MMIOFunc : public FindMMIOFunc::MMIOFunc {
    MMIOFunc(const FindMMIOFunc::MMIOFunc &Parent)
      : FindMMIOFunc::MMIOFunc(Parent), IsLib(false), IsHal(false),
        CalledByApp(false), Caller(nullptr), CallI(nullptr) {}
    bool IsLib; // library or application func
    bool IsHal; // Is HAL func. Valid only for lib func
    bool CalledByApp;
    const llvm::Function *Caller;
    const llvm::CallInst *CallI;
  };
  using Result = std::map<const llvm::Function *, MMIOFunc>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  Result runOnModule(llvm::Module &M, const FindMMIOFunc::Result &);
  // Part of the official API:
  //  https://llvm.org/docs/WritingAnLLVMNewPMPass.html#required-passes
  static bool isRequired() { return true; }

private:
  // A special type used by analysis passes to provide an address that
  // identifies that particular analysis pass type.
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<FindHALBypass>;

  bool isHalFunc(const llvm::Function &F);
  bool isLibFunc(const llvm::Function &F);
  bool isHal(const std::string &Str);
  void checkCalledByApp(llvm::Module &M, Result &MMIOFuncs);
};

//------------------------------------------------------------------------------
// New PM interface for the printer pass
//------------------------------------------------------------------------------
class FindHALBypassPrinter
    : public llvm::PassInfoMixin<FindHALBypassPrinter> {
public:
  explicit FindHALBypassPrinter(llvm::raw_ostream &OutS) : OS(OutS) {}
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  // Part of the official API:
  //  https://llvm.org/docs/WritingAnLLVMNewPMPass.html#required-passes
  static bool isRequired() { return true; }

private:
  llvm::raw_ostream &OS;
};

//------------------------------------------------------------------------------
// Legacy PM interface
//------------------------------------------------------------------------------
//struct LegacyFindHALBypass : public llvm::ModulePass {
//  static char ID;
//  LegacyFindHALBypass() : llvm::ModulePass(ID) {}
//  bool runOnModule(llvm::Module &M) override;
//  // The print method must be implemented by Legacy analysis passes in order to
//  // print a human readable version of the analysis results:
//  //    http://llvm.org/docs/WritingAnLLVMPass.html#the-print-method
//  void print(llvm::raw_ostream &OutS, llvm::Module const *M) const override;
//
//  ResultStaticCC DirectCalls;
//  FindHALBypass Impl;
//};

#endif // LLVM_TUTOR_FINDHALBYPASS_H
