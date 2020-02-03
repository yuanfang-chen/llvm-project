//===---------- PassManager.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the pass management machinery for machine functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/PassManager.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

using namespace llvm;

namespace llvm {
template class AllAnalysesOn<MachineFunction>;
template class AnalysisManager<MachineFunction>;
template class PassManager<MachineFunction>;

void MachineFunctionPassManager::run(const Module &M,
                                     MachineFunctionIRAnalysisManager &MFAM) {
  MachineModuleInfo &MMI = MFAM.getResult<MachineModuleAnalysis>(M);

  for (auto F : InitializationFuncs)
    F(M);

  PreservedAnalyses PA = PreservedAnalyses::all();
  for (auto &P : Passes) {
    for (const Function &F : M) {
      // Do not codegen any 'available_externally' functions at all, they have
      // definitions outside the translation unit.
      if (F.hasAvailableExternallyLinkage())
        continue;

      MachineFunction &MF = MMI.getOrCreateMachineFunction(F);
      PreservedAnalyses PassPA = P->run(MF, MFAM);
      MFAM.invalidate(MF, PassPA);
    }
  }

  for (auto F : FinalizationFuncs)
    F(M);
}

} // namespace llvm
