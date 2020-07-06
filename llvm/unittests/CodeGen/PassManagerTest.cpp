//===- llvm/unittest/CodeGen/PassManager.cpp - PassManager tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class TestFunctionAnalysis : public AnalysisInfoMixin<TestFunctionAnalysis> {
public:
  struct Result {
    Result(int Count) : InstructionCount(Count) {}
    int InstructionCount;
  };

  /// Run the analysis pass over the function and return a result.
  Result run(Function &F, FunctionAnalysisManager &AM) {
    int Count = 0;
    for (Function::iterator BBI = F.begin(), BBE = F.end(); BBI != BBE; ++BBI)
      for (BasicBlock::iterator II = BBI->begin(), IE = BBI->end(); II != IE;
           ++II)
        ++Count;
    return Result(Count);
  }

private:
  friend AnalysisInfoMixin<TestFunctionAnalysis>;
  static AnalysisKey Key;
};

AnalysisKey TestFunctionAnalysis::Key;

class TestMachineFunctionAnalysis
    : public AnalysisInfoMixin<TestMachineFunctionAnalysis> {
public:
  struct Result {
    Result(int Count) : InstructionCount(Count) {}
    int InstructionCount;
  };

  /// Run the analysis pass over the machine function and return a result.
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager::Base &AM) {
    auto &MFAM = static_cast<MachineFunctionAnalysisManager&>(AM);
    // Query function analysis result.
    TestFunctionAnalysis::Result &FAR =
        MFAM.getResult<TestFunctionAnalysis>(MF.getFunction());
    // + 5
    return FAR.InstructionCount;
  }

private:
  friend AnalysisInfoMixin<TestMachineFunctionAnalysis>;
  static AnalysisKey Key;
};

AnalysisKey TestMachineFunctionAnalysis::Key;

struct TestMachineFunctionPass : public PassInfoMixin<TestMachineFunctionPass> {
  TestMachineFunctionPass(int &Count, int &BeforeInitialization,
                          int &BeforeFinalization,
                          int &MachineFunctionPassCount)
      : Count(Count), BeforeInitialization(BeforeInitialization),
        BeforeFinalization(BeforeFinalization),
        MachineFunctionPassCount(MachineFunctionPassCount) {}

  Error doInitialization(Module &M, MachineFunctionAnalysisManager &MFAM) {
    // + 1
    ++Count;
    BeforeInitialization = Count;
    return Error::success();
  }
  Error doFinalization(Module &M, MachineFunctionAnalysisManager &MFAM) {
    // + 1
    ++Count;
    BeforeFinalization = Count;
    return Error::success();
  }

  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM) {
    // Query function analysis result.
    TestFunctionAnalysis::Result &FAR =
        MFAM.getResult<TestFunctionAnalysis>(MF.getFunction());
    // + 5
    Count += FAR.InstructionCount;

    // Query module analysis result.
    MachineModuleInfo &MMI =
        MFAM.getResult<MachineModuleAnalysis>(*MF.getFunction().getParent());
    // + 3
    Count += (MMI.getModule() == MF.getFunction().getParent());

    // Query machine function analysis result.
    TestMachineFunctionAnalysis::Result &MFAR =
        MFAM.getResult<TestMachineFunctionAnalysis>(MF);
    // + 5
    Count += MFAR.InstructionCount;

    MachineFunctionPassCount = Count;

    return PreservedAnalyses::none();
  }

  int &Count;
  int &BeforeInitialization;
  int &BeforeFinalization;
  int &MachineFunctionPassCount;
};

struct TestMachineModulePass : public PassInfoMixin<TestMachineModulePass> {
  TestMachineModulePass(int &Count, int &MachineModulePassCount)
      : Count(Count), MachineModulePassCount(MachineModulePassCount) {}

  Error run(Module &M, MachineFunctionAnalysisManager &MFAM) {
    MachineModuleInfo &MMI = MFAM.getResult<MachineModuleAnalysis>(M);
    // + 1
    Count += (MMI.getModule() == &M);
    MachineModulePassCount = Count;
    return Error::success();
  }

  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &AM) {
    llvm_unreachable("This should never be reached. This function is a "
                     "placeholder to make machine pass manager "
                     "less intrusive with this feature.");
  }

  int &Count;
  int &MachineModulePassCount;
};

std::unique_ptr<Module> parseIR(LLVMContext &Context, const char *IR) {
  SMDiagnostic Err;
  return parseAssemblyString(IR, Err, Context);
}

class PassManagerTest : public ::testing::Test {
protected:
  LLVMContext Context;
  std::unique_ptr<Module> M;
  std::unique_ptr<TargetMachine> TM;

public:
  PassManagerTest()
      : M(parseIR(Context, "define void @f() {\n"
                           "entry:\n"
                           "  call void @g()\n"
                           "  call void @h()\n"
                           "  ret void\n"
                           "}\n"
                           "define void @g() {\n"
                           "  ret void\n"
                           "}\n"
                           "define void @h() {\n"
                           "  ret void\n"
                           "}\n")) {
    // MachineModuleAnalysis needs a TargetMachine instance.
    llvm::InitializeAllTargets();

    std::string Error;
    const Target *TheTarget =
        TargetRegistry::lookupTarget("x86_64-unknown-linux", Error);
    // If we didn't build x86, do not run the test.
    if (!TheTarget)
      return;

    TargetOptions Options;
    TM.reset(TheTarget->createTargetMachine("x86_64-unknown-linux", "", "",
                                            Options, None));
  }
};

TEST_F(PassManagerTest, Basic) {
  LLVMTargetMachine *LLVMTM = static_cast<LLVMTargetMachine *>(TM.get());
  M->setDataLayout(TM->createDataLayout());

  LoopAnalysisManager LAM(/*DebugLogging*/ true);
  FunctionAnalysisManager FAM(/*DebugLogging*/ true);
  CGSCCAnalysisManager CGAM(/*DebugLogging*/ true);
  ModuleAnalysisManager MAM(/*DebugLogging*/ true);
  PassBuilder PB(TM.get());
  PB.registerModuleAnalyses(MAM);
  PB.registerFunctionAnalyses(FAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  FAM.registerPass([&] { return TestFunctionAnalysis(); });
  FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  MAM.registerPass([&] { return MachineModuleAnalysis(LLVMTM); });
  MAM.registerPass([&] { return PassInstrumentationAnalysis(); });

  MachineFunctionAnalysisManager MFAM;
  {
    // Test move assignment.
    MachineFunctionAnalysisManager NestedMFAM(FAM, MAM,
                                              /*DebugLogging*/ true);
    NestedMFAM.registerPass([&] { return PassInstrumentationAnalysis(); });
    NestedMFAM.registerPass([&] { return TestMachineFunctionAnalysis(); });
    MFAM = std::move(NestedMFAM);
  }

  int Count = 0;
  int BeforeInitialization;
  int BeforeFinalization;
  int TestMachineFunctionCount;
  int TestMachineModuleCount1;
  int TestMachineModuleCount2;

  MachineFunctionPassManager MFPM;
  {
    // Test move assignment.
    MachineFunctionPassManager NestedMFPM(/*DebugLogging*/ true);
    NestedMFPM.addPass(TestMachineModulePass(Count, TestMachineModuleCount1));
    NestedMFPM.addPass(TestMachineFunctionPass(Count, BeforeInitialization,
                                               BeforeFinalization,
                                               TestMachineFunctionCount));
    NestedMFPM.addPass(TestMachineModulePass(Count, TestMachineModuleCount2));
    MFPM = std::move(NestedMFPM);
  }

  cantFail(MFPM.run(*M, MFAM));

  EXPECT_EQ(1, BeforeInitialization);
  EXPECT_EQ(2, TestMachineModuleCount1);
  EXPECT_EQ(15, TestMachineFunctionCount);
  EXPECT_EQ(16, TestMachineModuleCount2);
  EXPECT_EQ(17, BeforeFinalization);
  EXPECT_EQ(17, Count);
}

} // namespace
