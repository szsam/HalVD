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
#if 0
/* Already defined in commandline */
#define _GNU_SOURCE /* memrchr() */
#endif
#include <unistd.h>

#include "FindHALBypass.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <algorithm>
#include <regex>
#include <random>
#include <queue>
#include <set>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <cstring>

using namespace llvm;

// Pretty-prints the result of this analysis
static void printHALBypassResult(llvm::raw_ostream &OutS,
                                 const FindHALBypass::Result &);
static std::string resolvePath(StringRef Dir, StringRef Filename);

//------------------------------------------------------------------------------
// FindHALBypass Implementation
//------------------------------------------------------------------------------
FindHALBypass::Result
FindHALBypass::runOnModule(Module &M, const FindMMIOFunc::Result &MMIOFuncs) {
  MMIOFuncMap.clear();
  for (auto &Node : MMIOFuncs) {
    const Function *F = Node.first;
    MMIOFunc MF = MMIOFunc(Node.second, F);
    MMIOFuncMap.insert({F, MF});
  }
  CallGraph CG = CallGraph(M);
  callGraphBasedHalIdent(CG);

  return MMIOFuncMap;
}

FindHALBypass::MMIOFunc::MMIOFunc(const FindMMIOFunc::MMIOFunc &Parent,
                                  const Function *F)
    : FindMMIOFunc::MMIOFunc(Parent), F(F), IsHalPattern(false), NCMA_CG(false),
      InDegree(0), TransClosureInDeg(0) {
  DISubprogram *DISub = F->getSubprogram();
  if (!DISub) return;
  DIFile *File = DISub->getFile();
  std::string Filename(File->getFilename());
  std::string Dir(File->getDirectory());
  //FullPath = Dir + "/" + Filename;
  FullPath = resolvePath(File->getDirectory(), File->getFilename());
  size_t Found = FullPath.find_last_of("/\\");
  Dirname = FullPath.substr(0, Found);
  isHalPattern();
}

void FindHALBypass::MMIOFunc::isHalPattern() {
  DISubprogram *DISub = F->getSubprogram();
  if (!DISub) {
    errs() << "Warning: isHalFunc: DISubprogram not exists.\n";
    return;
  }

  std::string Name(DISub->getName());
  std::string LinkageName(DISub->getLinkageName());

//  IsHalPattern = isHalPatternInternal(Name)
//              || isHalPatternInternal(LinkageName)
//              || isHalPatternInternal(FullPath);
  NCMA_GroundTruth = !MacroUsed && !(isHalPatternInternal(Name, true)
                                  || isHalPatternInternal(LinkageName, true)
                                  || isHalPatternInternal(FullPath, true));
    //MY_DEBUG(dbgs() << "Hal function: " << DISub->getName() << " "
    //                << LinkageName << " " << FullPath << "\n");
}

bool FindHALBypass::MMIOFunc::isHalPatternInternal(std::string Name, bool Full) {
  std::regex ProjRe("Amazfitbip-FreeRTOS|RP2040-FreeRTOS|"
                    "(blockingmqtt|dualport|ipcommdevice)_freertos");
  Name = std::regex_replace(Name, ProjRe, "");

  std::string HalReStr =
    "(?!.*zephyr/samples)" // Does not contain "zephyr/samples"
    "(?!.*hal_examples)" // Does not contain "hal_examples"
    ".*(^|[^[:alpha:]])" // Word boundary
    "(hal|drivers?|cmsis|arch|soc|boards?|irq|isr"
    "|port(able)?|spi|hardware|timer|nvic"
    "";
  if (Full) {
    HalReStr +=
    // below are project-specific patterns
    "|npl"  // NimBLE Porting Layer (NPL)
    "|nrfx"  // peripheral drivers for Nordic SoCs
    "|libopencm3"
    "|zephyr/subsys/bluetooth/controller"
    "|mbed-os/targets"
    //"|mbed-os/platform"
    "|avm"  // Avem/libs/module/avm_*.[ch]
    "|plo/devices" // phoenix-rtos
    "|esp-idf/components/(esp_hw_support|esp_system|bootloader_support|"
    "esp_phy|esp_timer|ulp|esp_psram|esp_rom)"
    "|system_stm32f4xx\\.c"  // STM32_BASE
    // Debug purpose
    //"|print_context_info" // mbed
    //"|nx_start_application" // nuttx
    "";
  }
  HalReStr += ")($|[^[:alpha:]]).*";
  return std::regex_match(Name, std::regex(HalReStr, std::regex::icase));
}

void FindHALBypass::callGraphBasedHalIdent(llvm::CallGraph &CG) {
  computeCallGraphInDeg(CG);
  computeCallGraphTCInDeg(CG);
  std::set<std::string> HalDirs;
  for (auto &I : MMIOFuncMap) {
    I.second.NCMA_CG = !I.second.MacroUsed;
    //if (I.second.TransClosureInDeg >= CallGraphTCInDegPctl(75.0)) {
    if (I.second.TransClosureInDeg >= 10) {
      HalDirs.insert(I.second.Dirname);
    }
  }
  for (auto &I : MMIOFuncMap) {
    if (HalDirs.find(I.second.Dirname) != HalDirs.end() ) {
      I.second.NCMA_CG = false;
    }
  }
//  auto CntTruePos = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return I.second.IsHal && I.second.IsHal2; });
//  auto CntSelected = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return I.second.IsHal2; });
//  auto CntRelevant = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return I.second.IsHal; });
//  errs() << "HAL precision=" << CntTruePos << "/" << CntSelected << "="
//         << static_cast<float>(CntTruePos) / CntSelected
//         << " recall=" << CntTruePos << "/" << CntRelevant << "="
//         << static_cast<float>(CntTruePos) / CntRelevant
//         << "\n";
//
//  auto CntTruePosN = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return !I.second.IsHal && !I.second.IsHal2; });
//  auto CntSelectedN = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return !I.second.IsHal2; });
//  auto CntRelevantN = std::count_if(MMIOFuncMap.begin(), MMIOFuncMap.end(),
//      [](auto &I) { return !I.second.IsHal; });
//  errs() << "Non-HAL precision=" << CntTruePosN << "/" << CntSelectedN << "="
//         << static_cast<float>(CntTruePosN) / CntSelectedN
//         << " recall=" << CntTruePosN << "/" << CntRelevantN << "="
//         << static_cast<float>(CntTruePosN) / CntRelevantN
//         << "\n";
}

void FindHALBypass::computeCallGraphTCInDeg(llvm::CallGraph &CG) {
  //CG.dump();
  std::map<CallGraphNode *, int> CGN2Num;
  int TotNumOfCGN= 0;

  for (auto &I : CG) {
    CGN2Num[I.second.get()] = TotNumOfCGN++;
  }
  CGN2Num[CG.getCallsExternalNode()] = TotNumOfCGN++;
  std::vector<int> AdjMatrix(TotNumOfCGN * TotNumOfCGN, 0);

  int NumOfEdges = 0;
  for (auto &I : CG) {
    //const Function *Caller = I.first;
    CallGraphNode *Caller = I.second.get();
    for (auto &J : *I.second) {
      //const Function *Callee = J.second->getFunction();
      CallGraphNode *Callee = J.second;
      AdjMatrix[CGN2Num.at(Caller) * TotNumOfCGN + CGN2Num.at(Callee)] = 1;
      NumOfEdges++;
    }
  }
  //dbgs() << "#vertices=" << TotNumOfCGN << " #edges=" << NumOfEdges << "\n";
  CGNumOfNodes = TotNumOfCGN;
  CGNumOfEdges = NumOfEdges;

  //std::vector<int> InDegrees = runFloydWarshall(AdjMatrix, TotNumOfCGN);
  std::vector<int> InDegrees = runTCEst(AdjMatrix, TotNumOfCGN);

  for (auto &I : MMIOFuncMap) {
    I.second.TransClosureInDeg = InDegrees[CGN2Num.at(CG[I.first])];
  }
}

std::vector<int> FindHALBypass::runFloydWarshall(
    std::vector<int> &AdjMatrix, int TotNumOfCGN) {
  for (int K = 0; K < TotNumOfCGN; K++) {
    for (int I = 0; I < TotNumOfCGN; I++) {
      for (int J = 0; J < TotNumOfCGN; J++) {
        // reach[i][j] = reach[i][j] || (reach[i][k] && reach[k][j]);
        AdjMatrix[I * TotNumOfCGN + J] = AdjMatrix[I * TotNumOfCGN + J] ||
          (AdjMatrix[I * TotNumOfCGN + K] && AdjMatrix[K * TotNumOfCGN + J]);
      }
    }
  }

  std::vector<int> InDegrees(TotNumOfCGN, 0);
  for (int I = 0; I < TotNumOfCGN; I++) {
    std::transform(AdjMatrix.begin() + I * TotNumOfCGN,
                   AdjMatrix.begin() + (I+1) * TotNumOfCGN,
                   InDegrees.begin(), InDegrees.begin(), std::plus<int>());
  }
  return InDegrees;
}

std::vector<int> FindHALBypass::runTCEst(
    std::vector<int> &AdjMatrix, int TotNumOfCGN) {
  // Transpose AdjMatrix
  //for (int I = 0; I < TotNumOfCGN; I++) {
  //  for (int J = 0; J < I; J++) {
  //    // swap AdjMatrix[I][J] and AdjMatrix[J][I]
  //    std::swap(AdjMatrix[I * TotNumOfCGN + J], AdjMatrix[J * TotNumOfCGN + I]);
  //  }
  //}

  std::vector<double> RankLeastSum(TotNumOfCGN, 0.0);
  std::vector<int> InDegrees(TotNumOfCGN);
  int NumOfIter = 10;
  for (int I = 0; I < NumOfIter; I++) {
    auto RankLeast = runTCEstOneIter(AdjMatrix, TotNumOfCGN);
    std::transform(RankLeast.begin(), RankLeast.end(), RankLeastSum.begin(),
                   RankLeastSum.begin(), std::plus<double>());
  }
  std::transform(RankLeastSum.begin(), RankLeastSum.end(), InDegrees.begin(),
                 [NumOfIter](double s) {
                   return std::round(NumOfIter / s) - 1;
                 });
  return InDegrees;
}

std::vector<double> FindHALBypass::runTCEstOneIter(
    std::vector<int> &AdjMatrix, int TotNumOfCGN) {
  std::random_device RD;
  std::mt19937 Gen(RD());
  std::uniform_real_distribution<> UniformDis(0.0, 1.0);
  std::vector<std::pair<int, double>> Rank;
  Rank.reserve(TotNumOfCGN);
  for (int I = 0; I < TotNumOfCGN; I++) {
    Rank.push_back({I, UniformDis(Gen)});
  }
  std::sort(Rank.begin(), Rank.end(), [](auto &LHS, auto &RHS) {
      return LHS.second < RHS.second;
  });
  //for (auto &I : Rank) {
  //  dbgs() << I.first << " " << I.second << "\n";
  //}

  std::vector<double> RankLeast(TotNumOfCGN, 0.0);
  std::vector<int> Visited(TotNumOfCGN, 0);
  for (auto &Src : Rank) {
    // BFS on Src.first
    if (Visited[Src.first])
      continue;
    std::queue<int> BFSQueue;

    BFSQueue.push(Src.first);
    Visited[Src.first] = 1;
    RankLeast[Src.first] = Src.second;

    while (!BFSQueue.empty()) {
      int U = BFSQueue.front();
      BFSQueue.pop();
      for (int V = 0; V < TotNumOfCGN; V++) {
        if (AdjMatrix[U * TotNumOfCGN + V] == 0 || Visited[V])
          continue;
        BFSQueue.push(V);
        Visited[V] = 1;
        RankLeast[V] = Src.second;
      }
    }
  }
  return RankLeast;
}

void FindHALBypass::computeCallGraphInDeg(llvm::CallGraph &CG) {
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

int FindHALBypass::CallGraphTCInDegPctl(double percent) {
  std::vector<int> InDegs;
  InDegs.reserve(MMIOFuncMap.size());

  for (const auto &I : MMIOFuncMap)
    InDegs.push_back(I.second.TransClosureInDeg);

  auto Nth = InDegs.begin() + percent / 100.0 * InDegs.size();
  std::nth_element(InDegs.begin(), Nth, InDegs.end());
  return *Nth;
}

PreservedAnalyses FindHALBypassPrinter::run(Module &M,
                                            ModuleAnalysisManager &MAM) {

  auto &Res = MAM.getResult<FindHALBypass>(M);

  printHALBypassResult(OS, Res);
  return PreservedAnalyses::all();
}

FindHALBypass::Result FindHALBypass::run(llvm::Module &M,
                                         llvm::ModuleAnalysisManager &MAM) {
  auto start_time = std::chrono::high_resolution_clock::now();
  //auto &Funcs = MAM.getResult<FindMMIOFunc>(M);
  auto Res = runOnModule(M, MAM.getResult<FindMMIOFunc>(M));
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  dbgs() << "# of Node vs. Analysis time: " << CGNumOfNodes << " " << duration.count() << "\n";
  return Res;
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
// Copied from https://stackoverflow.com/questions/4774116/realpath-without-resolving-symlinks
static char * normalize_path(const char * src, size_t src_len) {
  char * res;
  size_t res_len;

  const char * ptr = src;
  const char * end = &src[src_len];
  const char * next;

  if (src_len == 0 || src[0] != '/') {

	// relative path

	char pwd[PATH_MAX];
	size_t pwd_len;

	if (getcwd(pwd, sizeof(pwd)) == NULL) {
	  return NULL;
	}

	pwd_len = strlen(pwd);
	res = (char *)malloc(pwd_len + 1 + src_len + 1);
    if (!res) return NULL;
	memcpy(res, pwd, pwd_len);
	res_len = pwd_len;
  } else {
	res = (char *)malloc((src_len > 0 ? src_len : 1) + 1);
    if (!res) return NULL;
	res_len = 0;
  }

  for (ptr = src; ptr < end; ptr=next+1) {
	size_t len;
	next = (const char *)memchr(ptr, '/', end-ptr);
	if (next == NULL) {
	  next = end;
	}
	len = next-ptr;
	switch(len) {
	  case 2:
		if (ptr[0] == '.' && ptr[1] == '.') {
		  const char * slash = (const char *)memrchr(res, '/', res_len);
		  if (slash != NULL) {
			res_len = slash - res;
		  }
		  continue;
		}
		break;
	  case 1:
		if (ptr[0] == '.') {
		  continue;

		}
		break;
	  case 0:
		continue;
	}
	res[res_len++] = '/';
	memcpy(&res[res_len], ptr, len);
	res_len += len;
  }

  if (res_len == 0) {
	res[res_len++] = '/';
  }
  res[res_len] = '\0';
  return res;
}

static std::string resolvePath(StringRef Dir, StringRef Filename) {
  std::string FullPath = std::string(Dir) + "/" + std::string(Filename);
  char *ResolvedPath = normalize_path(FullPath.c_str(), FullPath.size());
  std::string Ret = ResolvedPath ? std::string(ResolvedPath) : FullPath;
  free(ResolvedPath);
  return Ret;
}

static void printDebugLoc(raw_ostream &OS, const DebugLoc &DL) {
  if (!DL)
    return;

   // Print source line info.
   auto *Scope = cast<DIScope>(DL.getScope());
   OS << resolvePath(Scope->getDirectory(), Scope->getFilename());
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
                       const char *Str, const char *Head) {
  OutS << "================================================="
       << "\n";
  OutS << "LLVM-TUTOR: " << Str << " (# = " << MMIOFuncs.size() << ")\n";
  OutS << "Function, Location of MMIO inst, TC In-degree, NCMA(CG), NCMA(truth), Macro\n";
  OutS << "-------------------------------------------------"
       << "\n";

  for (auto &Node : MMIOFuncs) {
    OutS << Head << ": ";
    OutS << Node.first->getName() << " ";
    printDebugLoc(OutS, Node.second.MMIOIns->getDebugLoc());
    //OutS << " " << Node.second.InDegree;
    OutS << " " << Node.second.TransClosureInDeg;
    //OutS << " " << Node.second.IsHalPattern;
    OutS << " " << Node.second.NCMA_CG;
    OutS << " " << Node.second.NCMA_GroundTruth;
    OutS << " " << Node.second.MacroUsed;
    OutS << "\n";
  }

  OutS << "-------------------------------------------------"
       << "\n\n";
}

static inline void printStatistics(raw_ostream &OutS, const char *Caption,
                                   size_t S1, size_t S2) {
  OutS << Caption<< S1 << "/" << S2 << "=" << static_cast<float>(S1) / S2 << " ";
}

static void printHALBypassResult(raw_ostream &OutS,
                                 const FindHALBypass::Result &MMIOFuncs) {
  //FindHALBypass::Result AppFuncs;
  //FindHALBypass::Result LibHalFuncs;
  //FindHALBypass::Result TPFuncs, FPFuncs, FNFuncs, TNFuncs;
  FindHALBypass::Result NonConv, Conv;
  for (auto &Node : MMIOFuncs) {
    //if (!Node.second.IsHalPattern && !Node.second.IsHalCG)
    if (Node.second.NCMA_GroundTruth)
      NonConv.insert(Node);
    //else if (Node.second.IsHal && !Node.second.IsHal2)
    //  FPFuncs.insert(Node);
    //else if (!Node.second.IsHal && Node.second.IsHal2)
    //  FNFuncs.insert(Node);
    else
      Conv.insert(Node);
  }
  printFuncs(OutS, NonConv, "Non-conventional MMIO functions", "Non-HAL");
  printFuncs(OutS, Conv, "Conventional (HAL) MMIO functions", "HAL");
  //printFuncs(OutS, TPFuncs, "True Positive: Non-conventional MMIO functions");
  //printFuncs(OutS, FPFuncs, "False Positive: Incorrectly identified as Non-conventional MMIO functions");
  //printFuncs(OutS, FNFuncs, "False Negative: Missed Non-conventional MMIO functions");
  //printFuncs(OutS, TNFuncs, "True Negative: Conventional (HAL) MMIO functions");
  //printStatistics(OutS, "precision(PPV)=", TPFuncs.size(), TPFuncs.size() + FPFuncs.size());
  //printStatistics(OutS, "recall(TPR)="   , TPFuncs.size(), TPFuncs.size() + FNFuncs.size());
  //printStatistics(OutS, "NPV="           , TNFuncs.size(), TNFuncs.size() + FNFuncs.size());
  //printStatistics(OutS, "TNR="           , TNFuncs.size(), TNFuncs.size() + FPFuncs.size());
}
