//===- NewPMDriver.cpp - Driver for llc using new PM ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file is just a split of the code that logically belongs in llc.cpp but
/// that includes the new pass manager headers.
///
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/CodeGen/CGPassBuilderOption.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

static cl::opt<std::string> PassPipeline(
    "passes", cl::desc("A textual description of the codegen IR pass pipeline"),
    cl::Hidden);

static cl::opt<RegAllocType> RegAlloc(
    "regalloc2", cl::desc("Register allocator to use for new pass manager"),
    cl::Hidden, cl::ValueOptional, cl::init(RegAllocType::Default),
    cl::values(
        clEnumValN(RegAllocType::Default, "default",
                   "pick register allocator based on -O option"),
        clEnumValN(RegAllocType::Basic, "basic", "basic register allocator"),
        clEnumValN(RegAllocType::Fast, "fast", "fast register allocator"),
        clEnumValN(RegAllocType::Greedy, "greedy", "greedy register allocator"),
        clEnumValN(RegAllocType::PBQP, "pbqp", "PBQP register allocator")));

static cl::opt<bool>
    DebugPM("debug-pass-manager", cl::Hidden,
            cl::desc("Print pass management debugging information"));

bool LLCDiagnosticHandler::handleDiagnostics(const DiagnosticInfo &DI) {
  if (DI.getSeverity() == DS_Error)
    *HasError = true;

  if (auto *Remark = dyn_cast<DiagnosticInfoOptimizationBase>(&DI))
    if (!Remark->isEnabled())
      return true;

  DiagnosticPrinterRawOStream DP(errs());
  errs() << LLVMContext::getDiagnosticMessagePrefix(DI.getSeverity()) << ": ";
  DI.print(DP);
  errs() << "\n";
  return true;
}

llvm::ExitOnError ExitOnErr;

static void RunPasses(bool CompileTwice, bool BOS, ToolOutputFile *Out,
                      Module *M, LLVMContext &Context,
                      SmallVector<char, 0> &Buffer, ModulePassManager *MPM,
                      ModuleAnalysisManager *MAM,
                      MachineFunctionPassManager *MFPM,
                      MachineFunctionAnalysisManager *MFAM) {
  auto RunPM = [=]() {
    if (MPM) {
      assert(MAM);
      MPM->run(*M, *MAM);
    }

    if (MFPM) {
      assert(MFAM);
      ExitOnErr(MFPM->run(*M, *MFAM));
    }
  };

  assert(M);

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  // If requested, run the pass manager over the same module again,
  // to catch any bugs due to persistent state in the passes. Note that
  // opt has the same functionality, so it may be worth abstracting this out
  // in the future.
  SmallVector<char, 0> CompileTwiceBuffer;
  if (CompileTwice) {
    std::unique_ptr<Module> M2(llvm::CloneModule(*M));
    RunPM();
    CompileTwiceBuffer = Buffer;
    Buffer.clear();
  }

  RunPM();

  auto HasError =
      ((const LLCDiagnosticHandler *)(Context.getDiagHandlerPtr()))->HasError;
  if (*HasError)
    exit(1);

  // Compare the two outputs and make sure they're the same
  if (CompileTwice) {
    if (Buffer.size() != CompileTwiceBuffer.size() ||
        (memcmp(Buffer.data(), CompileTwiceBuffer.data(), Buffer.size()) !=
         0)) {
      errs() << "Running the pass manager twice changed the output.\n"
                "Writing the result of the second run to the specified output\n"
                "To generate the one-run comparison binary, just run without\n"
                "the compile-twice option\n";
      Out->os() << Buffer;
      Out->keep();
      exit(1);
    }
  }

  if (BOS) {
    Out->os() << Buffer;
  }
}

int llvm::compileModuleWithNewPM(
    StringRef Arg0, std::unique_ptr<Module> M, std::unique_ptr<MIRParser> MIR,
    std::unique_ptr<TargetMachine> Target, std::unique_ptr<ToolOutputFile> Out,
    std::unique_ptr<ToolOutputFile> DwoOut, LLVMContext &Context,
    const TargetLibraryInfoImpl &TLII, bool NoVerify, bool CompileTwice,
    const std::vector<std::string> &RunPassNames, CodeGenFileType FileType) {

  if (!RunPassNames.empty() && PassPipeline.getNumOccurrences()) {
    WithColor::warning(errs(), Arg0)
        << "could not specify both -run-pass and -passes\n";
    return 1;
  }

  if (!RunPassNames.empty() || PassPipeline.getNumOccurrences())
    if (TargetPassConfig::hasLimitedCodeGenPipeline()) {
      WithColor::warning(errs(), Arg0)
          << "run-pass cannot be used with "
          << TargetPassConfig::getLimitedCodeGenPipelineReason(" and ")
          << ".\n";
      return 1;
    }

  LLVMTargetMachine &LLVMTM = static_cast<LLVMTargetMachine &>(*Target);

  {
    raw_pwrite_stream *OS = &Out->os();

    // Manually do the buffering rather than using buffer_ostream,
    // so we can memcmp the contents in CompileTwice mode
    SmallVector<char, 0> Buffer;
    std::unique_ptr<raw_svector_ostream> BOS;
    if ((codegen::getFileType() != CGFT_AssemblyFile &&
         !Out->os().supportsSeeking()) ||
        CompileTwice) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }

    // Fetch options from TargetPassConfig
    CGPassBuilderOption Opt = getCGPassBuilderOption();
    Opt.DisableVerify = NoVerify;
    Opt.DebugPM = DebugPM;
    Opt.RegAlloc = RegAlloc;

    PassInstrumentationCallbacks PIC;
    StandardInstrumentations SI;
    SI.registerCallbacks(PIC);
    registerCodeGenCallback(PIC);

    LoopAnalysisManager LAM(Opt.DebugPM);
    FunctionAnalysisManager FAM(Opt.DebugPM);
    CGSCCAnalysisManager CGAM(Opt.DebugPM);
    ModuleAnalysisManager MAM(Opt.DebugPM);
    PassBuilder PB(Target.get(), PipelineTuningOptions(), None, &PIC);
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    FAM.registerPass([&] { return TargetLibraryAnalysis(TLII); });
    MAM.registerPass([&] { return MachineModuleAnalysis(&LLVMTM); });

    MachineFunctionAnalysisManager MFAM(FAM, MAM, Opt.DebugPM);

    if (!RunPassNames.empty()) {
      // Construct a custom pass pipeline that starts after instruction
      // selection.

      if (!MIR) {
        WithColor::warning(errs(), Arg0) << "run-pass is for .mir file only.\n";
        return 1;
      }

      MachineFunctionPassManager MFPM = ExitOnErr(LLVMTM.parseMIRPipeline(
          llvm::join(RunPassNames, ","), Opt, MFAM, &PIC));
      MFPM.addPass(PrintMIRPass(*OS));

      auto &MMI = MFAM.getResult<MachineModuleAnalysis>(*M);
      if (MIR->parseMachineFunctions(*M, MMI))
        return 1;

      RunPasses(CompileTwice, BOS.get(), Out.get(), M.get(), Context, Buffer,
                nullptr, nullptr, &MFPM, &MFAM);
    } else if (PassPipeline.getNumOccurrences()) {
      // Construct a custom pass pipeline that ends before instruction
      // selection.

      ModulePassManager MPM =
          ExitOnErr(LLVMTM.parseIRPipeline(PassPipeline, Opt, MFAM, &PIC));
      MPM.addPass(PrintModulePass(*OS));

      RunPasses(CompileTwice, BOS.get(), Out.get(), M.get(), Context, Buffer,
                &MPM, &MAM, nullptr, nullptr);
    } else {
      std::pair<ModulePassManager, MachineFunctionPassManager> PMPair =
          ExitOnErr(
              LLVMTM.buildCodeGenPipeline(*OS, DwoOut ? &DwoOut->os() : nullptr,
                                          FileType, Opt, MFAM, &PIC));

      // Add printing pass according the pass type: IR or machine pass.
      std::string StopPass;
      if (!TargetPassConfig::willCompleteCodeGenPipeline(&StopPass)) {
        if (LLVMTM.isMachinePass(StopPass))
          PMPair.second.addPass(PrintMIRPass(*OS));
        else
          PMPair.first.addPass(PrintModulePass(*OS));
      }

      RunPasses(CompileTwice, BOS.get(), Out.get(), M.get(), Context, Buffer,
                &PMPair.first, &MAM, &PMPair.second, &MFAM);
    }
  }

  // Declare success.
  Out->keep();
  if (DwoOut)
    DwoOut->keep();

  return 0;
}
