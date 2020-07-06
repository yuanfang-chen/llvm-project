//===---------- MachinePassManager.cpp ------------------------------------===//
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

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/PassManagerImpl.h"

using namespace llvm;

namespace llvm {
template class AllAnalysesOn<MachineFunction>;
template class AnalysisManager<MachineFunction>;
template class PassManager<MachineFunction>;

Error MachineFunctionPassManager::run(Module &M,
                                      MachineFunctionAnalysisManager &MFAM) {
  // MachineModuleAnalysis is a module analysis pass that is never invalidated
  // because we don't run any module pass in codegen pipeline. This is very
  // important because the codegen state is stored in MMI which is the analysis
  // result of MachineModuleAnalysis. MMI should not be recomputed.
  auto &MMI = MFAM.getResult<MachineModuleAnalysis>(M);

  // TODO: implement RequireCodeGenSCCOrder
  (void)RequireCodeGenSCCOrder;

  if (DebugLogging)
    dbgs() << "Starting " << getTypeName<MachineFunction>()
           << " pass manager run.\n";

  for (auto &F : InitializationFuncs)
    if (auto Err = F(M, MFAM))
      return Err;

  for (unsigned Idx = 0, Size = Passes.size(); Idx != Size; ++Idx) {
    auto *P = Passes[Idx].get();

    // Run machine moulde passes
    if (HasRunOnModulePasses.count(Idx)) {
      if (DebugLogging)
        dbgs() << "Running pass: " << P->name() << " on " << M.getName()
               << "\n";
      if (auto Err = HasRunOnModulePasses.at(Idx)(M, MFAM))
        return Err;
      else
        continue;
    }

    for (Function &F : M) {
      // Do not codegen any 'available_externally' functions at all, they have
      // definitions outside the translation unit.
      if (F.hasAvailableExternallyLinkage())
        continue;

      MachineFunction &MF = MMI.getOrCreateMachineFunction(F);
      PassInstrumentation PI = MFAM.getResult<PassInstrumentationAnalysis>(MF);
      if (!PI.runBeforePass<MachineFunction>(*P, MF))
        continue;

      if (DebugLogging)
        dbgs() << "Running pass: " << P->name() << " on " << MF.getName()
               << "\n";

      // TODO: EmitSizeRemarks
      PreservedAnalyses PassPA = P->run(MF, MFAM);
      PI.runAfterPass(*P, MF);
      MFAM.invalidate(MF, PassPA);
    }
  }

  for (auto &F : FinalizationFuncs)
    if (auto Err = F(M, MFAM))
      return Err;

  if (DebugLogging)
    dbgs() << "Finished " << getTypeName<MachineFunction>()
           << " pass manager run.\n";

  return Error::success();
}

} // namespace llvm
