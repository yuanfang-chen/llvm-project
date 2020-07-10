; When EXPENSIVE_CHECKS are enabled, the machine verifier appears between each
; pass. Ignore it with 'grep -v'.
; RUN: llc -mtriple=x86_64-- -O0 -debug-pass-manager -enable-new-pm < %s \
; RUN:    -o /dev/null 2>&1 | grep -v 'Verify generated machine code' | FileCheck %s

; REQUIRES: asserts

; CHECK-LABEL: Running analysis: PassInstrumentationAnalysis
; CHECK-NEXT: Starting llvm::Module pass manager run.
; CHECK-NEXT: Running pass: PreISelIntrinsicLoweringPass
; CHECK-NEXT: Running pass: ModuleToFunctionPassAdaptor<{{.*}}PassManager{{.*}}>
; CHECK-NEXT: Running analysis: InnerAnalysisManagerProxy
; CHECK-NEXT: Running analysis: PassInstrumentationAnalysis
; CHECK-NEXT: Starting llvm::Function pass manager run.
; CHECK-NEXT: Running pass: AtomicExpandPass
; CHECK-NEXT: Running pass: VerifierPass
; CHECK-NEXT: Running analysis: VerifierAnalysis
; CHECK-NEXT: Running pass: GCLoweringPass
; CHECK-NEXT: Running pass: ShadowStackGCLoweringPass
; CHECK-NEXT: Running pass: LowerConstantIntrinsicsPass
; CHECK-NEXT: Running pass: UnreachableBlockElimPass
; CHECK-NEXT: Running pass: EntryExitInstrumenterPass
; CHECK-NEXT: Invalidating all non-preserved analyses for:
; CHECK-NEXT: Invalidating analysis: VerifierAnalysis
; CHECK-NEXT: Running pass: ScalarizeMaskedMemIntrinPass
; CHECK-NEXT: Running pass: ExpandReductionsPass
; CHECK-NEXT: Running analysis: TargetIRAnalysis
; CHECK-NEXT: Running pass: IndirectBrExpandPass
; CHECK-NEXT: Running pass: DwarfEHPass
; CHECK-NEXT: Running pass: SafeStackPass
; CHECK-NEXT: Running pass: StackProtectorPass
; CHECK-NEXT: Running pass: VerifierPass
; CHECK-NEXT: Running analysis: VerifierAnalysis
; CHECK-NEXT: Finished llvm::Function pass manager run.
; CHECK-NEXT: Invalidating all non-preserved analyses for:
; CHECK-NEXT: Finished llvm::Module pass manager run.
; CHECK-NEXT: Running analysis: MachineModuleAnalysis
; CHECK-NEXT: Starting llvm::MachineFunction pass manager run.
; CHECK-NEXT: Running analysis: PassInstrumentationAnalysis
; CHECK-NEXT: Running pass: {{.*}}X86ISelDagPass
; CHECK-NEXT: Running pass: {{.*}}X86GlobalBaseRegPass
; CHECK-NEXT: Running pass: FinalizeISelPass
; CHECK-NEXT: Running pass: LocalStackSlotPass
; CHECK-NEXT: Running pass: {{.*}}X86SpeculativeLoadHardeningPass
; CHECK-NEXT: Running pass: {{.*}}X86FlagsCopyLoweringDummyPass
; CHECK-NEXT: Running pass: {{.*}}X86WinAllocaExpanderPass
; CHECK-NEXT: Running pass: PHIEliminationPass
; CHECK-NEXT: Running pass: TwoAddressInstructionPass
; CHECK-NEXT: Running pass: RAFastPass
; CHECK-NEXT: Running pass: {{.*}}X86FloatingPointStackifierPass
; CHECK-NEXT: Running pass: PrologEpilogInserterPass
; CHECK-NEXT: Running pass: ExpandPostRAPseudosPass
; CHECK-NEXT: Running pass: {{.*}}X86ExpandPseudoPass
; CHECK-NEXT: Running pass: FEntryInserterPass
; CHECK-NEXT: Running pass: XRayInstrumentationPass
; CHECK-NEXT: Running pass: PatchableFunctionPass
; CHECK-NEXT: Running pass: {{.*}}X86IndirectBranchTrackingPass
; CHECK-NEXT: Running pass: {{.*}}X86IssueVZeroUpperPass
; CHECK-NEXT: Running pass: {{.*}}X86EvexToVexInstsPass
; CHECK-NEXT: Running pass: {{.*}}X86DiscriminateMemOpsPass
; CHECK-NEXT: Running pass: {{.*}}X86InsertPrefetchPass
; CHECK-NEXT: Running pass: {{.*}}X86InsertX87waitPass
; CHECK-NEXT: Running pass: FuncletLayoutPass
; CHECK-NEXT: Running pass: StackMapLivenessPass
; CHECK-NEXT: Running pass: LiveDebugValuesPass
; CHECK-NEXT: Running pass: {{.*}}X86RetpolineThunksPass
; CHECK-NEXT: Running pass: CFIInstrInserterPass
; CHECK-NEXT: Running pass: {{.*}}X86AsmPrinterPass
; CHECK-NEXT: Finished llvm::MachineFunction pass manager run.

define void @f() {
  ret void
}
