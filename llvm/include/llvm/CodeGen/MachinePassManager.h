//===- PassManager.h --- Pass management for CodeGen ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the pass manager interface for codegen. The codegen
// pipeline consists of only machine function passes. There is no container
// relationship between IR module/function and machine function in terms of pass
// manager organization. So there is no need for adaptor classes (for example
// ModuleToMachineFunctionAdaptor). Since invalidation could only happen among
// machine function passes, there is no proxy classes to handle cross-IR-unit
// invalidation. IR analysis results are provided for machine function passes by
// their respective analysis managers such as ModuleAnalysisManager and
// FunctionAnalysisManager.
//
// `doInitilization`/`doFinalization` are available like they do in legacy pass
// manager. This is for machine function passes to work on module level
// constructs. One such pass is AsmPrinter.
//
// Machine pass could also run over the module (call it machine module pass
// here). Passes using this API includes MachineOutliner, MachineDebugify etc..
//
// TODO: Add MachineFunctionProperties support.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PASS_MANAGER_H
#define LLVM_CODEGEN_PASS_MANAGER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/type_traits.h"

namespace llvm {
class Module;

extern template class AnalysisManager<MachineFunction>;

/// A AnalysisManager<MachineFunction> that also exposes IR analysis results.
class MachineFunctionAnalysisManager : public AnalysisManager<MachineFunction> {
public:
  using Base = AnalysisManager<MachineFunction>;

  MachineFunctionAnalysisManager() : Base(false), FAM(nullptr), MAM(nullptr) {}
  MachineFunctionAnalysisManager(FunctionAnalysisManager &FAM,
                                 ModuleAnalysisManager &MAM,
                                 bool DebugLogging = false)
      : Base(DebugLogging), FAM(&FAM), MAM(&MAM) {}
  MachineFunctionAnalysisManager(MachineFunctionAnalysisManager &&) = default;
  MachineFunctionAnalysisManager &
  operator=(MachineFunctionAnalysisManager &&) = default;

  // Register additional IR function analysis
  template <typename PassBuilderT>
  bool registerFunctionAnalysisPass(PassBuilderT &&PassBuilder) {
    return FAM->registerPass(PassBuilder);
  }

  // Register additional IR module analysis
  template <typename PassBuilderT>
  bool registerModuleAnalysisPass(PassBuilderT &&PassBuilder) {
    return MAM->registerPass(PassBuilder);
  }

  // Query IR function analysis
  template <typename PassT>
  typename PassT::Result &getResult(const Function &F) {
    return FAM->getResult<PassT>(const_cast<Function &>(F));
  }
  template <typename PassT>
  typename PassT::Result *getCachedResult(const Function &F) {
    return FAM->getCachedResult<PassT>(const_cast<Function &>(F));
  }

  // Query IR module analysis
  template <typename PassT> typename PassT::Result &getResult(const Module &M) {
    return MAM->getResult<PassT>(const_cast<Module &>(M));
  }
  template <typename PassT>
  typename PassT::Result *getCachedResult(const Module &M) {
    return MAM->getCachedResult<PassT>(const_cast<Module &>(M));
  }

  // Query machine function analysis
  using Base::getCachedResult;
  using Base::getResult;

private:
  // FIXME: Add LoopAnalysisManager or CGSCCAnalysisManager if needed.
  FunctionAnalysisManager *FAM;
  ModuleAnalysisManager *MAM;
};

extern template class PassManager<MachineFunction>;

class MachineFunctionPassManager
    : public PassManager<MachineFunction, MachineFunctionAnalysisManager> {
  using Base = PassManager<MachineFunction, MachineFunctionAnalysisManager>;

public:
  MachineFunctionPassManager(bool DebugLogging = false,
                             bool RequireCodeGenSCCOrder = false)
      : Base(DebugLogging), RequireCodeGenSCCOrder(RequireCodeGenSCCOrder) {}
  MachineFunctionPassManager(MachineFunctionPassManager &&) = default;
  MachineFunctionPassManager &
  operator=(MachineFunctionPassManager &&) = default;

  /// Entry point for codegen.
  Error run(Module &M, MachineFunctionAnalysisManager &MFAM);

  template <typename PassT> void addPass(PassT &&Pass) {
    Base::addPass(std::forward<PassT>(Pass));
    PassConceptT *P = Passes.back().get();
    addDoInitialization<PassT>(P);
    addDoFinalization<PassT>(P);

    // Add machine module pass.
    // Machine module pass need to define a method:
    // `Error run(Module &, MachineFunctionAnalysisManager &)`.
    // FIXME: machine module passes still need the usual machine function pass
    //        interface, namely,
    //        `PreservedAnalyses run(MachineFunction &,
    //                               MachineFunctionAnalysisManager &)`
    //        But this interface wouldn't be executed. It is just a placeholder
    //        to satisfy the pass manager type-erased inteface. This
    //        special-casing of machine module pass is due to its limited use
    //        cases and the unnecessary complexity it may bring to the machine
    //        pass manager.
    addRunOnModule<PassT>(P);
  }

private:
  template <typename PassT>
  using has_init_t = decltype(std::declval<PassT &>().doInitialization(
      std::declval<Module &>(),
      std::declval<MachineFunctionAnalysisManager &>()));

  template <typename PassT>
  std::enable_if_t<!is_detected<has_init_t, PassT>::value>
  addDoInitialization(PassConceptT *Pass) {}

  template <typename PassT>
  std::enable_if_t<is_detected<has_init_t, PassT>::value>
  addDoInitialization(PassConceptT *Pass) {
    using PassModelT =
        detail::PassModel<MachineFunction, PassT, PreservedAnalyses,
                          MachineFunctionAnalysisManager>;
    auto *P = static_cast<PassModelT *>(Pass);
    InitializationFuncs.emplace_back(
        [=](Module &M, MachineFunctionAnalysisManager &MFAM) {
          return P->Pass.doInitialization(M, MFAM);
        });
  }

  template <typename PassT>
  using has_fini_t = decltype(std::declval<PassT &>().doFinalization(
      std::declval<Module &>(),
      std::declval<MachineFunctionAnalysisManager &>()));

  template <typename PassT>
  std::enable_if_t<!is_detected<has_fini_t, PassT>::value>
  addDoFinalization(PassConceptT *Pass) {}

  template <typename PassT>
  std::enable_if_t<is_detected<has_fini_t, PassT>::value>
  addDoFinalization(PassConceptT *Pass) {
    using PassModelT =
        detail::PassModel<MachineFunction, PassT, PreservedAnalyses,
                          MachineFunctionAnalysisManager>;
    auto *P = static_cast<PassModelT *>(Pass);
    FinalizationFuncs.emplace_back(
        [=](Module &M, MachineFunctionAnalysisManager &MFAM) {
          return P->Pass.doFinalization(M, MFAM);
        });
  }

  template <typename PassT>
  using is_machine_module_pass_t = decltype(std::declval<PassT &>().run(
      std::declval<Module &>(),
      std::declval<MachineFunctionAnalysisManager &>()));

  template <typename PassT>
  std::enable_if_t<!is_detected<is_machine_module_pass_t, PassT>::value>
  addRunOnModule(PassConceptT *Pass) {}

  template <typename PassT>
  std::enable_if_t<is_detected<is_machine_module_pass_t, PassT>::value>
  addRunOnModule(PassConceptT *Pass) {
    using PassModelT =
        detail::PassModel<MachineFunction, PassT, PreservedAnalyses,
                          MachineFunctionAnalysisManager>;
    auto *P = static_cast<PassModelT *>(Pass);
    HasRunOnModulePasses.emplace(
        Passes.size() - 1,
        [=](Module &M, MachineFunctionAnalysisManager &MFAM) {
          return P->Pass.run(M, MFAM);
        });
  }

  using FuncTy = Error(Module &, MachineFunctionAnalysisManager &);
  SmallVector<llvm::unique_function<FuncTy>, 4> InitializationFuncs;
  SmallVector<llvm::unique_function<FuncTy>, 4> FinalizationFuncs;

  // A set of `Passes` indexes where matching pass has defined `run(Module&,..)`
  using PassIndex = decltype(Passes)::size_type;
  std::map<PassIndex, llvm::unique_function<FuncTy>> HasRunOnModulePasses;

  // Force codegen to run according to the callgraph.
  bool RequireCodeGenSCCOrder;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PASS_MANAGER_H
