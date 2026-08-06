	# wasm_sjlj_32.s — setjmp/longjmp support for the wasm32 target (BUGS #173)
	#
	# LLVM's WebAssembly SjLj lowering (`-mllvm -wasm-enable-sjlj`) is an IR-level
	# transform: at each `call @setjmp` it emits `__wasm_setjmp`, and it wraps the
	# body in a wasm `try`/`catch __c_longjmp` region whose handler calls
	# `__wasm_setjmp_test` to decide whether *this* frame's setjmp is the target.
	# A `call @longjmp` becomes `__wasm_longjmp`, which `throw`s the `__c_longjmp`
	# exception tag. This is a pure-wasm mechanism (wasm exception-handling
	# proposal) — it needs NO JS host imports.
	#
	# These four support symbols normally ship in compiler-rt's
	# libclang_rt.builtins-wasm32.a. That library is not part of the Homebrew LLVM
	# install this project links against, so we provide them here. Assemble with
	# `-mexception-handling` (the `throw`/`.tagtype` instructions require it); the
	# driver adds that flag on the same clang line.
	#
	# ABI note: Saffron's codegen allocates a fresh jmp_buf per `try` and calls
	# `setjmp` on it exactly once (see codegen/expr_body.sf and stmts_body.sf), so
	# a jmp_buf holds a single {label, funcInvocationId} pair inline rather than the
	# growable table compiler-rt maintains for the general C case. Nested try/catch
	# uses distinct buffers, which __wasm_setjmp_test discriminates by
	# funcInvocationId.

	# The wasm exception tag longjmp is thrown/caught with. The SjLj-transformed
	# user code emits `catch __c_longjmp`; sharing the exact symbol name is what
	# lets the linker unify the two into one tag index.
	.globl	__c_longjmp
	.tagtype	__c_longjmp i32
__c_longjmp:

	# void __wasm_setjmp(i8* env, i32 label, i8* funcInvocationId)
	# Record this setjmp: env is { i32 label; i8* funcInvocationId }.
	.globl	__wasm_setjmp
	.type	__wasm_setjmp,@function
__wasm_setjmp:
	.functype	__wasm_setjmp (i32, i32, i32) -> ()
	local.get	0
	local.get	1
	i32.store	0
	local.get	0
	local.get	2
	i32.store	4
	end_function

	# i32 __wasm_setjmp_test(i8* env, i8* funcInvocationId)
	# Return env->label if env->funcInvocationId matches (this frame is the
	# longjmp target), else 0 (keep unwinding to an outer handler).
	.globl	__wasm_setjmp_test
	.type	__wasm_setjmp_test,@function
__wasm_setjmp_test:
	.functype	__wasm_setjmp_test (i32, i32) -> (i32)
	local.get	0
	i32.load	4
	local.get	1
	i32.eq
	if	i32
	local.get	0
	i32.load	0
	else
	i32.const	0
	end_if
	end_function

	# void __wasm_longjmp(i8* env, i32 val)
	# Throw __c_longjmp carrying a pointer to { i8* env; i32 val }. The SjLj
	# catch handler reads env back out to run __wasm_setjmp_test.
	.globl	__wasm_longjmp
	.type	__wasm_longjmp,@function
__wasm_longjmp:
	.functype	__wasm_longjmp (i32, i32) -> ()
	i32.const	__wasm_longjmp_args
	local.get	0
	i32.store	0
	i32.const	__wasm_longjmp_args
	local.get	1
	i32.store	4
	i32.const	__wasm_longjmp_args
	throw	__c_longjmp
	end_function

	.type	__wasm_longjmp_args,@object
	.section	.bss.__wasm_longjmp_args,"",@
	.globl	__wasm_longjmp_args
	.p2align	2, 0x0
__wasm_longjmp_args:
	.int32	0
	.int32	0
	.size	__wasm_longjmp_args, 8

	# Advertise the exception-handling feature so the assembled object agrees
	# with the SjLj-transformed user object at link time.
	.section	.custom_section.target_features,"",@
	.int8	1
	.int8	43
	.int8	18
	.ascii	"exception-handling"
