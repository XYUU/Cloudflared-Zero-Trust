/* cmake/stubs/atomic_mips32.c
 *
 * PIC-safe 8-byte __sync built-in stubs for MIPS32 (mipsel).
 *
 * Ubuntu's mipsel-linux-gnu libatomic.a is compiled without -fPIC, so it
 * cannot be statically embedded into libmsquic.so: the linker rejects the
 * R_MIPS_HI16 absolute relocations with "recompile with -fPIC".
 *
 * These three symbols are the ones msquic emits on 32-bit MIPS.  They are
 * implemented via a compiler-generated spinlock on a 32-bit word.  GCC
 * lowers __atomic_compare_exchange_n(<int>) to inline LL/SC on MIPS II+
 * (all Linux MIPS targets), so there is no recursive libatomic dependency.
 *
 * Added to the msquic target via target_sources() in cmake/msquic.cmake.
 * The msquic target has CMAKE_POSITION_INDEPENDENT_CODE=ON so this file
 * is compiled with -fPIC automatically.
 */

#include <stdint.h>
#include <string.h>

static int _s8_lock;

static void _s8_acquire(void)
{
    int zero;
    do {
        zero = 0;
    } while (!__atomic_compare_exchange_n(
                 &_s8_lock, &zero, 1,
                 /*weak=*/0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}

static void _s8_release(void)
{
    __atomic_store_n(&_s8_lock, 0, __ATOMIC_RELEASE);
}

__attribute__((visibility("hidden")))
uint64_t __sync_val_compare_and_swap_8(volatile void *ptr,
                                        uint64_t oldval, uint64_t newval)
{
    uint64_t cur;
    _s8_acquire();
    memcpy(&cur, (const void *)ptr, sizeof cur);
    if (cur == oldval)
        memcpy((void *)ptr, &newval, sizeof newval);
    _s8_release();
    return cur;
}

__attribute__((visibility("hidden")))
uint64_t __sync_fetch_and_add_8(volatile void *ptr, uint64_t val)
{
    uint64_t cur, next;
    _s8_acquire();
    memcpy(&cur, (const void *)ptr, sizeof cur);
    next = cur + val;
    memcpy((void *)ptr, &next, sizeof next);
    _s8_release();
    return cur;
}

__attribute__((visibility("hidden")))
uint64_t __sync_add_and_fetch_8(volatile void *ptr, uint64_t val)
{
    uint64_t cur, next;
    _s8_acquire();
    memcpy(&cur, (const void *)ptr, sizeof cur);
    next = cur + val;
    memcpy((void *)ptr, &next, sizeof next);
    _s8_release();
    return next;
}
