//===- PassManager.h --- Pass management for CodeGen ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface for the pass manager functionality used for CodeGen. The CodeGen
// pipeline consists of only machine function passes. There is no container
// relationship between IR module/function and machine function in terms of pass
// manager organization. So there is no need for adaptor classes (for example
// ModuleToMachineFunctionAdaptor). Since invalidation could only happen among
// machine function passes, there is no proxy classes to handle cross-IR-unit
// invalidation. IR analysis results are provided for machine function passes by
// their respective analysis managers such as ModuleAnalysisManager and
// FunctionAnalysisManager.
//
// doInitilization/doFinalization are available like they do in legacy pass
// manager. This is mostly for AsmPrinter. Their uses in other passes could be
// converted easily to use either constructor or lazy initialization in `run`
// method.
//
// TODO: Add MachineFunctionProperties support.
// TODO: Add PassInstrumentation function. O/W substitutePass/InsertPass/DisablePass does not work.
// TODO: Add a path in CodeGen to experiment with this interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PASS_MANAGER_H
#define LLVM_CODEGEN_PASS_MANAGER_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
extern template class AnalysisManager<MachineFunction>;

/// Machine function passes use this by default.
using MachineFunctionAnalysisManager = AnalysisManager<MachineFunction>;

/// Expose IR analysis results to machine function pass.
class MachineFunctionIRAnalysisManager : public MachineFunctionAnalysisManager {
  // Add LoopAnalysisManager or CGSCCAnalysisManager in the future if needed.
  FunctionAnalysisManager *FAM;
  ModuleAnalysisManager *MAM;

public:
  MachineFunctionIRAnalysisManager(bool DebugLogging = false,
                                   FunctionAnalysisManager *FAM = nullptr,
                                   ModuleAnalysisManager *MAM = nullptr)
      : MachineFunctionAnalysisManager(DebugLogging), FAM(FAM), MAM(MAM) {}
  MachineFunctionIRAnalysisManager(MachineFunctionIRAnalysisManager &&) =
      default;
  MachineFunctionIRAnalysisManager &
  operator=(MachineFunctionIRAnalysisManager &&) = default;

  template <typename PassT> typename PassT::Result &getResult(const Module &M) {
    assert(MAM);
    return MAM->getResult<PassT>(const_cast<Module &>(M));
  }
  template <typename PassT>
  typename PassT::Result &getResult(const Function &F) {
    assert(FAM);
    return FAM->getResult<PassT>(const_cast<Function &>(F));
  }
};

extern template class PassManager<MachineFunction>;

class MachineFunctionPassManager : public PassManager<MachineFunction> {
public:
  MachineFunctionPassManager(bool DebugLogging = false)
      : PassManager<MachineFunction>(DebugLogging) {}
  MachineFunctionPassManager(MachineFunctionPassManager &&) = default;
  MachineFunctionPassManager &
  operator=(MachineFunctionPassManager &&) = default;

  /// Entry point for codegen.
  void run(const Module &M, MachineFunctionIRAnalysisManager &MFAM);

  template <
      typename PassT,
      bool HasDoInitialization =
          detail::PassHasDoInitializationMethod<PassT>::value,
      bool HasDoFinalizatio =
          detail::PassHasDoFinalizationMethod<PassT>::value>
  void addPass(PassT Pass) {
    using PassModelT =
        detail::PassModel<MachineFunction, PassT, PreservedAnalyses,
                          MachineFunctionAnalysisManager>;

    PassManager<MachineFunction>::addPass<PassT>(Pass);

    if (HasDoInitialization) {
      auto *P = reinterpret_cast<PassModelT *>(Passes.back().get());
      InitializationFuncs.push_back(
          [=](const Module &M) { P->Pass.doInitialization(M); });
    }

    if (HasDoFinalizatio) {
      auto *P = reinterpret_cast<PassModelT *>(Passes.back().get());
      FinalizationFuncs.push_back(
          [=](const Module &M) { P->Pass.doFinalization(M); });
    }
  }

private:
  std::vector<std::function<void(const Module &M)>> InitializationFuncs;
  std::vector<std::function<void(const Module &M)>> FinalizationFuncs;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PASS_MANAGER_H
