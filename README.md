# JIT (FFI) vs PLT benchmark

This is a simple benchmark for demonstrating the PLT function call
overhead. It requires x86-64, but will otherwise work on any POSIX
system.

Full article:
[When FFI Function Calls Beat Native C](http://nullprogram.com/blog/2018/05/27/)

Results on an Intel i7-6700 (Skylake):

    jit: 1.008108 ns/call
    plt: 1.759799 ns/call
    ind: 1.257125 ns/call

This is the assembly used for JIT benchmark:

```nasm
check:  dd 0

jit_benchmark:
	push  rbx
	xor   ebx, ebx
.loop:	mov   eax, [rel check]
	test  eax, eax
	je    .done
	call  empty
	inc   ebx
	jmp   .loop
.done:	mov   eax, ebx
	pop   rbx
	ret
```

The relative address for `empty` is patched in by the JIT compiler.
