#include "NewPMDriver.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/CommandFlags.inc"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/PassManager.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/RemarkStreamer.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include <memory>
using namespace llvm;

int compileModuleWithNewPM(M, ) {
  LoopAnalysisManager LAM(/*DebugLogging*/ true);
  FunctionAnalysisManager FAM(/*DebugLogging*/ true);
  CGSCCAnalysisManager CGAM(/*DebugLogging*/ true);
  ModuleAnalysisManager MAM(/*DebugLogging*/ true);
  MAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  MAM.registerPass([&] { return MachineModuleAnalysis(LLVMTM); });
  FAM.registerPass([&] { return PassInstrumentationAnalysis(); });
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

  {
    raw_pwrite_stream *OS = &Out->os();

    // Manually do the buffering rather than using buffer_ostream,
    // so we can memcmp the contents in CompileTwice mode
    SmallVector<char, 0> Buffer;
    std::unique_ptr<raw_svector_ostream> BOS;
    if ((FileType != CGFT_AssemblyFile &&
         !Out->os().supportsSeeking()) ||
        CompileTwice) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }

    const char *argv0 = argv[0];
    LLVMTargetMachine &LLVMTM = static_cast<LLVMTargetMachine &>(*Target);
    MachineModuleInfoWrapperPass *MMIWP =
        new MachineModuleInfoWrapperPass(&LLVMTM);

    // TODO:
    // Construct a custom pass pipeline that starts after instruction
    // selection.
    //if (!RunPassNames->empty()) {
    //   if (!MIR) {
    //     WithColor::warning(errs(), argv[0])
    //         << "run-pass is for .mir file only.\n";
    //     return 1;
    //   }
    //   TargetPassConfig &TPC = *LLVMTM.createPassConfig(PM);
    //   if (PartialPipelineConfig::hasLimitedCodeGenPipeline()) {
    //     WithColor::warning(errs(), argv[0])
    //         << "run-pass cannot be used with "
    //         << PartialPipelineConfig::getLimitedCodeGenPipelineReason(" and ") << ".\n";
    //     return 1;
    //   }

    //   TPC.setDisableVerify(NoVerify);
    //   PM.add(&TPC);
    //   PM.add(MMIWP);
    //   TPC.printAndVerify("");
    //   for (const std::string &RunPassName : *RunPassNames) {
    //     if (addPass(PM, argv0, RunPassName, TPC))
    //       return 1;
    //   }
    //   TPC.setInitialized();
    //   PM.add(createPrintMIRPass(*OS));
    //   PM.add(createFreeMachineFunctionPass());
    // } else 
    
    if (Target->addPassesToEmitFile(PM, *OS,
                                           DwoOut ? &DwoOut->os() : nullptr,
                                           FileType, NoVerify, MMIWP)) {
      WithColor::warning(errs(), argv[0])
          << "target does not support generation of this"
          << " file type!\n";
      return 1;
    }

    if (MIR) {
      assert(MMIWP && "Forgot to create MMIWP?");
      if (MIR->parseMachineFunctions(*M, MMIWP->getMMI()))
        return 1;
    }

    // Before executing passes, print the final values of the LLVM options.
    cl::PrintOptionValues();

    // If requested, run the pass manager over the same module again,
    // to catch any bugs due to persistent state in the passes. Note that
    // opt has the same functionality, so it may be worth abstracting this out
    // in the future.
    SmallVector<char, 0> CompileTwiceBuffer;
    if (CompileTwice) {
      std::unique_ptr<Module> M2(llvm::CloneModule(*M));
      PM.run(*M2);
      CompileTwiceBuffer = Buffer;
      Buffer.clear();
    }

    PM.run(*M);

    auto HasError =
        ((const LLCDiagnosticHandler *)(Context.getDiagHandlerPtr()))->HasError;
    if (*HasError)
      return 1;

    // Compare the two outputs and make sure they're the same
    if (CompileTwice) {
      if (Buffer.size() != CompileTwiceBuffer.size() ||
          (memcmp(Buffer.data(), CompileTwiceBuffer.data(), Buffer.size()) !=
           0)) {
        errs()
            << "Running the pass manager twice changed the output.\n"
               "Writing the result of the second run to the specified output\n"
               "To generate the one-run comparison binary, just run without\n"
               "the compile-twice option\n";
        Out->os() << Buffer;
        Out->keep();
        return 1;
      }
    }

    if (BOS) {
      Out->os() << Buffer;
    }
  }

  // Declare success.
  Out->keep();
  if (DwoOut)
    DwoOut->keep();

  return 0;
}