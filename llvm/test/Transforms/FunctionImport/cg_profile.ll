; Check that RAUW performed by IRMover during function importing does not
; generate bitcast in "CG Profile" related metadat nodes.
; RUN: opt -cg-profile -module-summary %s -o %t.bc
; RUN: opt -module-summary %p/Inputs/cg_profile.ll -o %t2.bc
; RUN: llvm-lto -thinlto -o %t3 %t.bc %t2.bc
; RUN: opt -function-import -print-imports -summary-file %t3.thinlto.bc %t.bc -o %t.o

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; %class.A is defined differently in %p/Inputs/cg_profile.ll. This is to trigger
; bitcast.
%class.A = type { i8 }

define void @foo() !prof !2 {
  call void @bar(%class.A* null)
  ret void
}

declare void @bar(%class.A*)

!llvm.module.flags = !{!1}

!1 = !{i32 1, !"EnableSplitLTOUnit", i32 0}
!2 = !{!"function_entry_count", i64 2753}
