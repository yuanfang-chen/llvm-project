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
#include "llvm/CodeGen/PassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
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
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &AM) {
    return Result(MF.getInstructionCount());
  }

private:
  friend AnalysisInfoMixin<TestMachineFunctionAnalysis>;
  static AnalysisKey Key;
};

AnalysisKey TestMachineFunctionAnalysis::Key;

struct TestMachineFunctionPass : PassInfoMixin<TestMachineFunctionPass> {
  TestMachineFunctionPass(int &Count, int &BeforeInitialization,
                          int &BeforeFinalization)
      : Count(Count), BeforeInitialization(BeforeInitialization),
        BeforeFinalization(BeforeFinalization) {}

  void doInitialization(const Module &M) { BeforeInitialization = Count; }
  void doFinalization(const Module &M) { BeforeFinalization = Count; }

  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &AM) {
    auto &IRAM = static_cast<MachineFunctionIRAnalysisManager &>(AM);

    // Query function analysis result.
    TestFunctionAnalysis::Result &FAR =
        IRAM.getResult<TestFunctionAnalysis>(MF.getFunction());
    // + 5
    Count += FAR.InstructionCount;

    // Query module analysis result.
    MachineModuleInfo &MMI =
        IRAM.getResult<MachineModuleAnalysis>(*MF.getFunction().getParent());
    // + 3
    Count += (MMI.getModule() == MF.getFunction().getParent());

    // Query machine function analysis result.
    TestMachineFunctionAnalysis::Result &MFAR =
        AM.getResult<TestMachineFunctionAnalysis>(MF);
    // + 0
    Count += MFAR.InstructionCount;

    return PreservedAnalyses::none();
  }

  int &Count;
  int &BeforeInitialization;
  int &BeforeFinalization;
};

std::unique_ptr<Module> parseIR(LLVMContext &Context, const char *IR) {
  SMDiagnostic Err;
  return parseAssemblyString(IR, Err, Context);
}

class PassManagerTest : public ::testing::Test {
protected:
  LLVMContext Context;
  std::unique_ptr<Module> M;

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
                           "}\n")) {}
};

TEST_F(PassManagerTest, Basic) {
  // Create LLVMTargetMachine to initilize MachineModuleAnalysis.
  std::string Error;
  const Target *T =
      TargetRegistry::lookupTarget("x86_64-unknown-linux", Error);
  assert(T);

  TargetOptions Options;
  std::unique_ptr<TargetMachine> TM(
      T->createTargetMachine("x86_64-unknown-linux", "", "", Options, None,
                             None, CodeGenOpt::Aggressive));
  LLVMTargetMachine *LLVMTM = static_cast<LLVMTargetMachine *>(TM.get());
  M->setDataLayout(TM->createDataLayout());

  LoopAnalysisManager LAM(/*DebugLogging*/ true);
  FunctionAnalysisManager FAM(/*DebugLogging*/ true);
  CGSCCAnalysisManager CGAM(/*DebugLogging*/ true);
  ModuleAnalysisManager MAM(/*DebugLogging*/ true);
  MAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  MAM.registerPass([&] { return MachineModuleAnalysis(LLVMTM); });
  FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  FAM.registerPass([&] { return TestFunctionAnalysis(); });
  PassBuilder PB;
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  MachineFunctionIRAnalysisManager MFAM;
  {
    // Test move assignment.
    MachineFunctionIRAnalysisManager NestedMFAM(/*DebugLogging*/ true, &FAM,
                                                &MAM);
    NestedMFAM.registerPass([&] { return PassInstrumentationAnalysis(); });
    NestedMFAM.registerPass([&] { return TestMachineFunctionAnalysis(); });
    MFAM = std::move(NestedMFAM);
  }

  int Count = 0;
  int BeforeInitialization;
  int BeforeFinalization;

  MachineFunctionPassManager MFPM;
  {
    // Test move assignment.
    MachineFunctionPassManager NestedMFPM(/*DebugLogging*/ true);
    NestedMFPM.addPass(TestMachineFunctionPass(Count, BeforeInitialization,
                                               BeforeFinalization));
    MFPM = std::move(NestedMFPM);
  }

  MFPM.run(*M, MFAM);

  EXPECT_EQ(8, Count);
  EXPECT_EQ(0, BeforeInitialization);
  EXPECT_EQ(8, BeforeFinalization);
}

}
