//========================================================================
// FILE:
//    FindMMIOFunc.h
//
// DESCRIPTION:
//    Declares the FindMMIOFunc Passes
//      * new pass manager interface
//      * legacy pass manager interface
//      * printer pass for the new pass manager
//
// License: MIT
//========================================================================
#ifndef LLVM_TUTOR_FINDMMIOFUNC_H
#define LLVM_TUTOR_FINDMMIOFUNC_H

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

struct FindMMIOFunc : public llvm::AnalysisInfoMixin<FindMMIOFunc> {
  using Result = std::vector<const llvm::Function *>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  Result runOnModule(llvm::Module &M);
  // Part of the official API:
  //  https://llvm.org/docs/WritingAnLLVMNewPMPass.html#required-passes
  static bool isRequired() { return true; }

private:
  // A special type used by analysis passes to provide an address that
  // identifies that particular analysis pass type.
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<FindMMIOFunc>;

  template <typename InstTy>
  bool isMMIOInst(llvm::Instruction *Ins);
};

//------------------------------------------------------------------------------
// New PM interface for the printer pass
//------------------------------------------------------------------------------
class FindMMIOFuncPrinter
    : public llvm::PassInfoMixin<FindMMIOFuncPrinter> {
public:
  explicit FindMMIOFuncPrinter(llvm::raw_ostream &OutS) : OS(OutS) {}
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
//struct LegacyFindMMIOFunc : public llvm::ModulePass {
//  static char ID;
//  LegacyFindMMIOFunc() : llvm::ModulePass(ID) {}
//  bool runOnModule(llvm::Module &M) override;
//  // The print method must be implemented by Legacy analysis passes in order to
//  // print a human readable version of the analysis results:
//  //    http://llvm.org/docs/WritingAnLLVMPass.html#the-print-method
//  void print(llvm::raw_ostream &OutS, llvm::Module const *M) const override;
//
//  ResultStaticCC DirectCalls;
//  FindMMIOFunc Impl;
//};

#endif // LLVM_TUTOR_FINDMMIOFUNC_H
