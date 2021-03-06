// Copyright 2020 Google LLC.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkVM_opts_DEFINED
#define SkVM_opts_DEFINED

#include "include/private/SkVx.h"
#include "src/core/SkVM.h"

// Ideally this is (x*y + 0x2000)>>14,
// but to let use vpmulhrsw we'll approximate that as ((x*y + 0x4000)>>15)<<1.
template <int N>
static inline skvx::Vec<N,int16_t> mul_q14(const skvx::Vec<N,int16_t>& x,
                                           const skvx::Vec<N,int16_t>& y) {
#if SK_CPU_SSE_LEVEL >= SK_CPU_SSE_LEVEL_AVX2
    if constexpr (N == 16) {
        return skvx::bit_pun<skvx::Vec<N,int16_t>>(_mm256_mulhrs_epi16(skvx::bit_pun<__m256i>(x),
                                                                       skvx::bit_pun<__m256i>(y)))
            << 1;
    }
#endif
#if SK_CPU_SSE_LEVEL >= SK_CPU_SSE_LEVEL_SSSE3
    if constexpr (N == 8) {
        return skvx::bit_pun<skvx::Vec<N,int16_t>>(_mm_mulhrs_epi16(skvx::bit_pun<__m128i>(x),
                                                                    skvx::bit_pun<__m128i>(y)))
            << 1;
    }
#endif
    // TODO: NEON specialization with vqrdmulh.s16?

    // Try to recurse onto the specializations above.
    if constexpr (N > 8) {
        return join(mul_q14(x.lo, y.lo),
                    mul_q14(x.hi, y.hi));
    }
    return skvx::cast<int16_t>((skvx::cast<int>(x) *
                                skvx::cast<int>(y) + 0x4000)>>15 ) <<1;
}

namespace SK_OPTS_NS {

    inline void interpret_skvm(const skvm::InterpreterInstruction insts[], const int ninsts,
                               const int nregs, const int loop,
                               const int strides[], const int nargs,
                               int n, void* args[]) {
        using namespace skvm;

        // We'll operate in SIMT style, knocking off K-size chunks from n while possible.
        // We noticed quad-pumping is slower than single-pumping and both were slower than double.
    #if SK_CPU_SSE_LEVEL >= SK_CPU_SSE_LEVEL_AVX2
        constexpr int K = 16;
    #else
        constexpr int K = 8;
    #endif
        using I32 = skvx::Vec<K, int>;
        using F32 = skvx::Vec<K, float>;
        using U64 = skvx::Vec<K, uint64_t>;
        using U32 = skvx::Vec<K, uint32_t>;
        using U16 = skvx::Vec<K, uint16_t>;
        using  U8 = skvx::Vec<K, uint8_t>;

        using I16x2 = skvx::Vec<2*K,  int16_t>;
        using U16x2 = skvx::Vec<2*K, uint16_t>;

        union Slot {
            F32   f32;
            I32   i32;
            U32   u32;
            I16x2 i16x2;
            U16x2 u16x2;
        };

        Slot                     few_regs[16];
        std::unique_ptr<char[]> many_regs;

        Slot* r = few_regs;

        if (nregs > (int)SK_ARRAY_COUNT(few_regs)) {
            // Annoyingly we can't trust that malloc() or new will work with Slot because
            // the skvx::Vec types may have alignment greater than what they provide.
            // We'll overallocate one extra register so we can align manually.
            many_regs.reset(new char[ sizeof(Slot) * (nregs + 1) ]);

            uintptr_t addr = (uintptr_t)many_regs.get();
            addr += alignof(Slot) -
                     (addr & (alignof(Slot) - 1));
            SkASSERT((addr & (alignof(Slot) - 1)) == 0);
            r = (Slot*)addr;
        }


        // Step each argument pointer ahead by its stride a number of times.
        auto step_args = [&](int times) {
            for (int i = 0; i < nargs; i++) {
                args[i] = (void*)( (char*)args[i] + times * strides[i] );
            }
        };

        int start = 0,
            stride;
        for ( ; n > 0; start = loop, n -= stride, step_args(stride)) {
            stride = n >= K ? K : 1;

            for (int i = start; i < ninsts; i++) {
                InterpreterInstruction inst = insts[i];

                // d = op(x,y/imm,z/imm)
                Reg   d = inst.d,
                      x = inst.x,
                      y = inst.y,
                      z = inst.z;
                int immy = inst.immy,
                    immz = inst.immz;

                // Ops that interact with memory need to know whether we're stride=1 or K,
                // but all non-memory ops can run the same code no matter the stride.
                switch (2*(int)inst.op + (stride == K ? 1 : 0)) {
                    default: SkUNREACHABLE;

                #define STRIDE_1(op) case 2*(int)op
                #define STRIDE_K(op) case 2*(int)op + 1
                    STRIDE_1(Op::store8 ): memcpy(args[immy], &r[x].i32, 1); break;
                    STRIDE_1(Op::store16): memcpy(args[immy], &r[x].i32, 2); break;
                    STRIDE_1(Op::store32): memcpy(args[immy], &r[x].i32, 4); break;
                    STRIDE_1(Op::store64): memcpy((char*)args[immz]+0, &r[x].i32, 4);
                                           memcpy((char*)args[immz]+4, &r[y].i32, 4); break;

                    STRIDE_K(Op::store8 ): skvx::cast<uint8_t> (r[x].i32).store(args[immy]); break;
                    STRIDE_K(Op::store16): skvx::cast<uint16_t>(r[x].i32).store(args[immy]); break;
                    STRIDE_K(Op::store32):                     (r[x].i32).store(args[immy]); break;
                    STRIDE_K(Op::store64): (skvx::cast<uint64_t>(r[x].u32) << 0 |
                                            skvx::cast<uint64_t>(r[y].u32) << 32).store(args[immz]);
                                           break;

                    STRIDE_1(Op::load8 ): r[d].i32 = 0; memcpy(&r[d].i32, args[immy], 1); break;
                    STRIDE_1(Op::load16): r[d].i32 = 0; memcpy(&r[d].i32, args[immy], 2); break;
                    STRIDE_1(Op::load32): r[d].i32 = 0; memcpy(&r[d].i32, args[immy], 4); break;
                    STRIDE_1(Op::load64):
                        r[d].i32 = 0; memcpy(&r[d].i32, (char*)args[immy] + 4*immz, 4); break;

                    STRIDE_K(Op::load8 ): r[d].i32= skvx::cast<int>(U8 ::Load(args[immy])); break;
                    STRIDE_K(Op::load16): r[d].i32= skvx::cast<int>(U16::Load(args[immy])); break;
                    STRIDE_K(Op::load32): r[d].i32=                 I32::Load(args[immy]) ; break;
                    STRIDE_K(Op::load64):
                        // Low 32 bits if immz=0, or high 32 bits if immz=1.
                        r[d].i32 = skvx::cast<int>(U64::Load(args[immy]) >> (32*immz)); break;

                    // The pointer we base our gather on is loaded indirectly from a uniform:
                    //     - args[immy] is the uniform holding our gather base pointer somewhere;
                    //     - (const uint8_t*)args[immy] + immz points to the gather base pointer;
                    //     - memcpy() loads the gather base and into a pointer of the right type.
                    // After all that we have an ordinary (uniform) pointer `ptr` to load from,
                    // and we then gather from it using the varying indices in r[x].
                    STRIDE_1(Op::gather8):
                        for (int i = 0; i < K; i++) {
                            const uint8_t* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = (i==0) ? ptr[ r[x].i32[i] ] : 0;
                        } break;
                    STRIDE_1(Op::gather16):
                        for (int i = 0; i < K; i++) {
                            const uint16_t* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = (i==0) ? ptr[ r[x].i32[i] ] : 0;
                        } break;
                    STRIDE_1(Op::gather32):
                        for (int i = 0; i < K; i++) {
                            const int* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = (i==0) ? ptr[ r[x].i32[i] ] : 0;
                        } break;

                    STRIDE_K(Op::gather8):
                        for (int i = 0; i < K; i++) {
                            const uint8_t* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = ptr[ r[x].i32[i] ];
                        } break;
                    STRIDE_K(Op::gather16):
                        for (int i = 0; i < K; i++) {
                            const uint16_t* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = ptr[ r[x].i32[i] ];
                        } break;
                    STRIDE_K(Op::gather32):
                        for (int i = 0; i < K; i++) {
                            const int* ptr;
                            memcpy(&ptr, (const uint8_t*)args[immy] + immz, sizeof(ptr));
                            r[d].i32[i] = ptr[ r[x].i32[i] ];
                        } break;

                #undef STRIDE_1
                #undef STRIDE_K

                    // Ops that don't interact with memory should never care about the stride.
                #define CASE(op) case 2*(int)op: /*fallthrough*/ case 2*(int)op+1

                    // These 128-bit ops are implemented serially for simplicity.
                    CASE(Op::store128): {
                        int ptr = immz>>1,
                            lane = immz&1;
                        U64 src = (skvx::cast<uint64_t>(r[x].u32) << 0 |
                                   skvx::cast<uint64_t>(r[y].u32) << 32);
                        for (int i = 0; i < stride; i++) {
                            memcpy((char*)args[ptr] + 16*i + 8*lane, &src[i], 8);
                        }
                    } break;

                    CASE(Op::load128):
                        r[d].i32 = 0;
                        for (int i = 0; i < stride; i++) {
                            memcpy(&r[d].i32[i], (const char*)args[immy] + 16*i+ 4*immz, 4);
                        } break;

                    CASE(Op::assert_true):
                    #ifdef SK_DEBUG
                        if (!all(r[x].i32)) {
                            SkDebugf("inst %d, register %d\n", i, y);
                            for (int i = 0; i < K; i++) {
                                SkDebugf("\t%2d: %08x (%g)\n", i, r[y].i32[i], r[y].f32[i]);
                            }
                            SkASSERT(false);
                        }
                    #endif
                    break;

                    CASE(Op::index): {
                        const int iota[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
                                            16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
                        static_assert(K <= SK_ARRAY_COUNT(iota), "");

                        r[d].i32 = n - I32::Load(iota);
                    } break;

                    CASE(Op::uniform8):
                        r[d].i32 = *(const uint8_t* )( (const char*)args[immy] + immz );
                        break;
                    CASE(Op::uniform16):
                        r[d].i32 = *(const uint16_t*)( (const char*)args[immy] + immz );
                        break;
                    CASE(Op::uniform32):
                        r[d].i32 = *(const int*     )( (const char*)args[immy] + immz );
                        break;

                    CASE(Op::splat): r[d].i32 = immy; break;

                    CASE(Op::add_f32): r[d].f32 = r[x].f32 + r[y].f32; break;
                    CASE(Op::sub_f32): r[d].f32 = r[x].f32 - r[y].f32; break;
                    CASE(Op::mul_f32): r[d].f32 = r[x].f32 * r[y].f32; break;
                    CASE(Op::div_f32): r[d].f32 = r[x].f32 / r[y].f32; break;
                    CASE(Op::min_f32): r[d].f32 = min(r[x].f32, r[y].f32); break;
                    CASE(Op::max_f32): r[d].f32 = max(r[x].f32, r[y].f32); break;

                    CASE(Op::fma_f32):  r[d].f32 = fma( r[x].f32, r[y].f32,  r[z].f32); break;
                    CASE(Op::fms_f32):  r[d].f32 = fma( r[x].f32, r[y].f32, -r[z].f32); break;
                    CASE(Op::fnma_f32): r[d].f32 = fma(-r[x].f32, r[y].f32,  r[z].f32); break;

                    CASE(Op::sqrt_f32): r[d].f32 = sqrt(r[x].f32); break;

                    CASE(Op::add_i32): r[d].i32 = r[x].i32 + r[y].i32; break;
                    CASE(Op::sub_i32): r[d].i32 = r[x].i32 - r[y].i32; break;
                    CASE(Op::mul_i32): r[d].i32 = r[x].i32 * r[y].i32; break;

                    CASE(Op::shl_i32): r[d].i32 = r[x].i32 << immy; break;
                    CASE(Op::sra_i32): r[d].i32 = r[x].i32 >> immy; break;
                    CASE(Op::shr_i32): r[d].u32 = r[x].u32 >> immy; break;

                    CASE(Op:: eq_f32): r[d].i32 = r[x].f32 == r[y].f32; break;
                    CASE(Op::neq_f32): r[d].i32 = r[x].f32 != r[y].f32; break;
                    CASE(Op:: gt_f32): r[d].i32 = r[x].f32 >  r[y].f32; break;
                    CASE(Op::gte_f32): r[d].i32 = r[x].f32 >= r[y].f32; break;

                    CASE(Op:: eq_i32): r[d].i32 = r[x].i32 == r[y].i32; break;
                    CASE(Op:: gt_i32): r[d].i32 = r[x].i32 >  r[y].i32; break;

                    CASE(Op::bit_and  ): r[d].i32 = r[x].i32 &  r[y].i32; break;
                    CASE(Op::bit_or   ): r[d].i32 = r[x].i32 |  r[y].i32; break;
                    CASE(Op::bit_xor  ): r[d].i32 = r[x].i32 ^  r[y].i32; break;
                    CASE(Op::bit_clear): r[d].i32 = r[x].i32 & ~r[y].i32; break;

                    CASE(Op::select): r[d].i32 = skvx::if_then_else(r[x].i32, r[y].i32, r[z].i32);
                                      break;

                    CASE(Op::pack):    r[d].u32 = r[x].u32 | (r[y].u32 << immz); break;

                    CASE(Op::ceil):   r[d].f32 =                    skvx::ceil(r[x].f32) ; break;
                    CASE(Op::floor):  r[d].f32 =                   skvx::floor(r[x].f32) ; break;
                    CASE(Op::to_f32): r[d].f32 = skvx::cast<float>(            r[x].i32 ); break;
                    CASE(Op::trunc):  r[d].i32 = skvx::cast<int>  (            r[x].f32 ); break;
                    CASE(Op::round):  r[d].i32 = skvx::cast<int>  (skvx::lrint(r[x].f32)); break;

                    CASE(Op::to_half):
                        r[d].i32 = skvx::cast<int>(skvx::to_half(r[x].f32));
                        break;
                    CASE(Op::from_half):
                        r[d].f32 = skvx::from_half(skvx::cast<uint16_t>(r[x].i32));
                        break;

                    CASE(Op::add_q14x2): r[d].i16x2 = r[x].i16x2 + r[y].i16x2; break;
                    CASE(Op::sub_q14x2): r[d].i16x2 = r[x].i16x2 - r[y].i16x2; break;
                    CASE(Op::mul_q14x2): r[d].i16x2 = mul_q14(r[x].i16x2, r[y].i16x2); break;

                    CASE(Op::shl_q14x2): r[d].i16x2 = r[x].i16x2 << immy; break;
                    CASE(Op::sra_q14x2): r[d].i16x2 = r[x].i16x2 >> immy; break;
                    CASE(Op::shr_q14x2): r[d].u16x2 = r[x].u16x2 >> immy; break;

                    CASE(Op::eq_q14x2): r[d].i16x2 = r[x].i16x2 == r[y].i16x2; break;
                    CASE(Op::gt_q14x2): r[d].i16x2 = r[x].i16x2 >  r[y].i16x2; break;

                    CASE(Op:: min_q14x2): r[d].i16x2 = min(r[x].i16x2, r[y].i16x2); break;
                    CASE(Op:: max_q14x2): r[d].i16x2 = max(r[x].i16x2, r[y].i16x2); break;
                    CASE(Op::umin_q14x2): r[d].u16x2 = min(r[x].u16x2, r[y].u16x2); break;

                    // Happily, Clang can see through this one and generates perfect code
                    // using vpavgw without any help from us!
                    CASE(Op::uavg_q14x2):
                        r[d].u16x2 = skvx::cast<uint16_t>( (skvx::cast<int>(r[x].u16x2) +
                                                            skvx::cast<int>(r[y].u16x2) + 1)>>1 );
                        break;
                #undef CASE
                }
            }
        }
    }

}  // namespace SK_OPTS_NS

#endif//SkVM_opts_DEFINED
