#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#if !defined(PAGESIZE)
#    define PAGESIZE       4096
#endif
#define PAGEMASK           (~(PAGESIZE - 1))
#define SAFETY_MARGIN      (4L * 1024 * 1024)
#define BENCHMARK_SECONDS  2
#define BENCHMARK_SAMPLES  4

void empty(void);

static volatile sig_atomic_t *running_ptr;
static void
alarm_handler(int signum)
{
    (void)signum;
    *running_ptr = 0;
}

volatile sig_atomic_t running;

static long
plt_benchmark(void)
{
    long count;
    for (count = 0; running; count++)
        empty();
    return count;
}

static long
indirect_benchmark(void (*f)(void))
{
    long count;
    for (count = 0; running; count++)
        f();
    return count;
}

struct jit_func {
    volatile sig_atomic_t *running;
    long (*benchmark)(void);
};

static void
jit_compile(struct jit_func *f, void (*empty)(void))
{
    int zero = open("/dev/zero", O_RDWR);
    if (zero == -1) {
        perror("/dev/zero");
        exit(EXIT_FAILURE);
    }

    /* Allocate two pages nearby for the JIT-compiled function.
     * The first page holds the alarm variable and the second holds the
     * function. These pages have different protections once the
     * JIT function is assembled.
     */
    void *desired = (void *)(((uintptr_t)empty - SAFETY_MARGIN) & PAGEMASK);
    int prot = PROT_READ | PROT_WRITE;
    size_t len = PAGESIZE * 2;
    unsigned char *p = mmap(desired, len, prot, MAP_PRIVATE, zero, 0);
    if (p == MAP_FAILED) {
        perror("/dev/zero");
        exit(EXIT_FAILURE);
    }
    close(zero);

    p += PAGESIZE;
    f->running = (void *)(p - sizeof(sig_atomic_t));
    f->benchmark = (long (*)(void))(p);

    // push rbx
    *p++ = 0x53;
    // xor ebx, ebx
    *p++ = 0x31;
    *p++ = 0xdb;
    // .loop:
    // mov eax, [rel f->running]
    *p++ = 0x8b;
    *p++ = 0x05;
    *p++ = 0xf3;
    *p++ = 0xff;
    *p++ = 0xff;
    *p++ = 0xff;
    // test eax, eax
    *p++ = 0x85;
    *p++ = 0xc0;
    // jz .done
    *p++ = 0x74;
    *p++ = 0x09;
    // call empty
    uintptr_t rel = (uintptr_t)empty - (uintptr_t)p - 5;
    *p++ = 0xe8;
    *p++ = rel >>  0;
    *p++ = rel >>  8;
    *p++ = rel >> 16;
    *p++ = rel >> 24;
    // inc ebx
    *p++ = 0xff;
    *p++ = 0xc3;
    // jmp .loop
    *p++ = 0xeb;
    *p++ = 0xed;
    // .done:
    // mov eax,ebx
    *p++ = 0x89;
    *p++ = 0xd8;
    // pop rbx
    *p++ = 0x5b;
    // ret
    *p++ = 0xc3;

    /* W^X flip */
    mprotect(f->benchmark, PAGESIZE, PROT_EXEC);
}

static void
jit_free(struct jit_func *f)
{
    munmap((char *)f->benchmark - PAGESIZE, PAGESIZE * 2);
}

int
main(void)
{
    /* Get pointer directly to empty() */
    void *emptyso = dlopen("./empty.so", RTLD_NOW);
    void *empty_sym = dlsym(emptyso, "empty");

    /* JIT benchmark */
    struct jit_func jit;
    jit_compile(&jit, empty_sym);
    long jit_count = 0;
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        running_ptr = jit.running;
        *running_ptr = 1;
        signal(SIGALRM, alarm_handler);
        alarm(BENCHMARK_SECONDS);
        long count = jit.benchmark();
        if (count > jit_count)
            jit_count = count;
    }
    jit_free(&jit);
    printf("jit: %f ns/call\n", 1e9 * BENCHMARK_SECONDS / jit_count);

    /* PLT benchmark */
    long plt_count = 0;
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        running_ptr = &running;
        *running_ptr = 1;
        signal(SIGALRM, alarm_handler);
        alarm(BENCHMARK_SECONDS);
        long count = plt_benchmark();
        if (count > plt_count)
            plt_count = count;
    }
    printf("plt: %f ns/call\n", 1e9 * BENCHMARK_SECONDS / plt_count);

    /* Indirect benchmark */
    long indirect_count = 0;
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        running_ptr = &running;
        *running_ptr = 1;
        signal(SIGALRM, alarm_handler);
        alarm(BENCHMARK_SECONDS);
        long count = indirect_benchmark(empty_sym);
        if (count > indirect_count)
            indirect_count = count;
    }
    printf("ind: %f ns/call\n", 1e9 * BENCHMARK_SECONDS / indirect_count);

    dlclose(emptyso);
}
