; When EXPENSIVE_CHECKS are enabled, the machine verifier appears between each
; pass. Ignore it with 'grep -v'.
; RUN: llc -mtriple=x86_64-- -O1 -debug-pass-manager -enable-new-pm < %s \
; RUN:    -o /dev/null 2>&1 | FileCheck %s
; RUN: llc -mtriple=x86_64-- -O2 -debug-pass-manager -enable-new-pm < %s \
; RUN:    -o /dev/null 2>&1 | FileCheck %s
; RUN: llc -mtriple=x86_64-- -O3 -debug-pass-manager -enable-new-pm < %s \
; RUN:    -o /dev/null 2>&1 | FileCheck %s

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
; CHECK-NEXT: Running pass: FunctionToLoopPassAdaptor<{{.*}}LoopStrengthReducePass{{.*}}>
; CHECK-NEXT: Starting llvm::Function pass manager run.
; CHECK-NEXT: Running pass: LoopSimplifyPass
; CHECK-NEXT: Running analysis: LoopAnalysis
; CHECK-NEXT: Running analysis: DominatorTreeAnalysis
; CHECK-NEXT: Running analysis: AssumptionAnalysis
; CHECK-NEXT: Running pass: LCSSAPass
; CHECK-NEXT: Finished llvm::Function pass manager run.
; CHECK-NEXT: Running pass: MergeICmpsPass
; CHECK-NEXT: Running analysis: TargetLibraryAnalysis
; CHECK-NEXT: Running analysis: TargetIRAnalysis
; CHECK-NEXT: Running analysis: AAManager
; CHECK-NEXT: Running pass: ExpandMemCmpPass
; CHECK-NEXT: Running pass: GCLoweringPass
; CHECK-NEXT: Running pass: ShadowStackGCLoweringPass
; CHECK-NEXT: Running pass: LowerConstantIntrinsicsPass
; CHECK-NEXT: Running pass: UnreachableBlockElimPass
; CHECK-NEXT: Running pass: ConstantHoistingPass
; CHECK-NEXT: Running analysis: BlockFrequencyAnalysis
; CHECK-NEXT: Running analysis: BranchProbabilityAnalysis
; CHECK-NEXT: Running analysis: PostDominatorTreeAnalysis
; CHECK-NEXT: Running analysis: OuterAnalysisManagerProxy
; CHECK-NEXT: Running pass: PartiallyInlineLibCallsPass
; CHECK-NEXT: Running pass: EntryExitInstrumenterPass
; CHECK-NEXT: Invalidating all non-preserved analyses for:
; CHECK-NEXT: Invalidating analysis: VerifierAnalysis
; CHECK-NEXT: Running pass: ScalarizeMaskedMemIntrinPass
; CHECK-NEXT: Running pass: ExpandReductionsPass
; CHECK-NEXT: Running pass: InterleavedAccessPass
; CHECK-NEXT: Running pass: IndirectBrExpandPass
; CHECK-NEXT: Running pass: CodeGenPreparePass
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
; CHECK-NEXT: Running pass: {{.*}}CleanupLocalDynamicTLSPass
; CHECK-NEXT: Running pass: {{.*}}X86GlobalBaseRegPass
; CHECK-NEXT: Running pass: FinalizeISelPass
; CHECK-NEXT: Running pass: {{.*}}X86DomainReassignmentPass
; CHECK-NEXT: Running pass: EarlyTailDuplicatePass
; CHECK-NEXT: Running pass: OptimizePHIsPass
; CHECK-NEXT: Running pass: StackColoringPass
; CHECK-NEXT: Running pass: LocalStackSlotPass
; CHECK-NEXT: Running pass: DeadMachineInstructionElimPass
; CHECK-NEXT: Running pass: EarlyIfConverterPass
; CHECK-NEXT: Running pass: MachineCombinerPass
; CHECK-NEXT: Running pass: {{.*}}X86CmovConverterDummyPass
; CHECK-NEXT: Running pass: EarlyMachineLICMPass
; CHECK-NEXT: Running pass: MachineCSEPass
; CHECK-NEXT: Running pass: MachineSinkingPass
; CHECK-NEXT: Running pass: PeepholeOptimizerPass
; CHECK-NEXT: Running pass: DeadMachineInstructionElimPass
; CHECK-NEXT: Running pass: LiveRangeShrinkPass
; CHECK-NEXT: Running pass: {{.*}}X86FixupSetCCPass
; CHECK-NEXT: Running pass: {{.*}}X86OptimizeLEAsPass
; CHECK-NEXT: Running pass: {{.*}}X86CallFrameOptimizationPass
; CHECK-NEXT: Running pass: {{.*}}X86AvoidStoreForwardingBlocksPass
; CHECK-NEXT: Running pass: {{.*}}X86SpeculativeLoadHardeningPass
; CHECK-NEXT: Running pass: {{.*}}X86FlagsCopyLoweringDummyPass
; CHECK-NEXT: Running pass: {{.*}}X86WinAllocaExpanderPass
; CHECK-NEXT: Running pass: DetectDeadLanesPass
; CHECK-NEXT: Running pass: ProcessImplicitDefsPass
; CHECK-NEXT: Running pass: PHIEliminationPass
; CHECK-NEXT: Running pass: TwoAddressInstructionPass
; CHECK-NEXT: Running pass: RegisterCoalescerPass
; CHECK-NEXT: Running pass: RenameIndependentSubregsPass
; CHECK-NEXT: Running pass: MachineSchedulerPass
; CHECK-NEXT: Running pass: RAGreedyPass
; CHECK-NEXT: Running pass: VirtRegRewriterPass
; CHECK-NEXT: Running pass: StackSlotColoringPass
; CHECK-NEXT: Running pass: {{.*}}X86FloatingPointStackifierPass
; CHECK-NEXT: Running pass: PostRAMachineSinkingPass
; CHECK-NEXT: Running pass: ShrinkWrapPass
; CHECK-NEXT: Running pass: PrologEpilogInserterPass
; CHECK-NEXT: Running pass: BranchFolderPass
; CHECK-NEXT: Running pass: TailDuplicatePass
; CHECK-NEXT: Running pass: MachineCopyPropagationPass
; CHECK-NEXT: Running pass: ExpandPostRAPseudosPass
; CHECK-NEXT: Running pass: {{.*}}X86ExpandPseudoPass
; CHECK-NEXT: Running pass: PostRASchedulerPass
; CHECK-NEXT: Running pass: MachineBlockPlacementPass
; CHECK-NEXT: Running pass: FEntryInserterPass
; CHECK-NEXT: Running pass: XRayInstrumentationPass
; CHECK-NEXT: Running pass: PatchableFunctionPass
; CHECK-NEXT: Running pass: {{.*}}X86ExecutionDomainFixPass
; CHECK-NEXT: Running pass: BreakFalseDepsPass
; CHECK-NEXT: Running pass: {{.*}}X86IndirectBranchTrackingPass
; CHECK-NEXT: Running pass: {{.*}}X86IssueVZeroUpperPass
; CHECK-NEXT: Running pass: {{.*}}X86FixupBWInstsPass
; CHECK-NEXT: Running pass: {{.*}}X86PadShortFunctionsPass
; CHECK-NEXT: Running pass: {{.*}}X86FixupLEAsPass
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
