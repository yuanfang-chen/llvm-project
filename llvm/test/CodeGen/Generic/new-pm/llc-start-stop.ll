; RUN: llc < %s -enable-new-pm -debug-pass-manager -stop-after=verify \
; RUN:     -o /dev/null 2>&1 | FileCheck %s -check-prefix=STOP-AFTER
; STOP-AFTER: Running pass: AtomicExpandPass
; STOP-AFTER-NEXT: Running pass: VerifierPass
; STOP-AFTER-NOT: Running pass:

; RUN: llc < %s -enable-new-pm -debug-pass-manager -stop-before=verify \
; RUN:     -o /dev/null 2>&1 | FileCheck %s -check-prefix=STOP-BEFORE
; STOP-BEFORE: Running pass: AtomicExpandPass
; STOP-BEFORE-NOT: Running pass:

; RUN: llc < %s -enable-new-pm -debug-pass-manager -start-after=verify \
; RUN:     -o /dev/null 2>&1 | FileCheck %s -check-prefix=START-AFTER
; START-AFTER: Running pass: ModuleToFunctionPassAdaptor
; START-AFTER: Starting llvm::Function pass manager run
; START-AFTER-NOT: Running pass:
; START-AFTER: Running pass: {{.*}}LoopStrengthReducePass

; RUN: llc < %s -enable-new-pm -debug-pass-manager -start-before=verify \
; RUN:    -o /dev/null 2>&1 | FileCheck %s -check-prefix=START-BEFORE
; START-BEFORE: Running pass: ModuleToFunctionPassAdaptor
; START-BEFORE: Starting llvm::Function pass manager run
; START-BEFORE-NOT: Running pass:
; START-BEFORE: Running pass: VerifierPass

; RUN: not --crash llc < %s -enable-new-pm -start-before=nonexistent -o /dev/null 2>&1 \
; RUN:    | FileCheck %s -check-prefix=NONEXISTENT-START-BEFORE
; RUN: not --crash llc < %s -enable-new-pm -stop-before=nonexistent -o /dev/null 2>&1 \
; RUN:    | FileCheck %s -check-prefix=NONEXISTENT-STOP-BEFORE
; RUN: not --crash llc < %s -enable-new-pm -start-after=nonexistent -o /dev/null 2>&1 \
; RUN:    | FileCheck %s -check-prefix=NONEXISTENT-START-AFTER
; RUN: not --crash llc < %s -enable-new-pm -stop-after=nonexistent -o /dev/null 2>&1 \
; RUN:    | FileCheck %s -check-prefix=NONEXISTENT-STOP-AFTER
; NONEXISTENT-START-BEFORE: "nonexistent" pass could not be found.
; NONEXISTENT-STOP-BEFORE: "nonexistent" pass could not be found.
; NONEXISTENT-START-AFTER: "nonexistent" pass could not be found.
; NONEXISTENT-STOP-AFTER: "nonexistent" pass could not be found.

; RUN: not --crash llc < %s -enable-new-pm -start-before=verify -start-after=verify \
; RUN:    -o /dev/null 2>&1 | FileCheck %s -check-prefix=DOUBLE-START
; RUN: not --crash llc < %s -enable-new-pm -stop-before=verify -stop-after=verify \
; RUN:    -o /dev/null 2>&1 | FileCheck %s -check-prefix=DOUBLE-STOP
; DOUBLE-START: start-before and start-after specified!
; DOUBLE-STOP: stop-before and stop-after specified!

define void @f() {
  ret void
}
