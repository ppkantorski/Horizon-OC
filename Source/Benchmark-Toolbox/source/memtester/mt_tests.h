#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned short mt_dividend;
extern volatile int mt_abort;

typedef int (*mt_test_fp)(unsigned long volatile *, unsigned long volatile *, size_t);

struct mt_test {
    const char *name;
    mt_test_fp fp;
};

extern struct mt_test mt_tests[];
extern struct mt_test mt_stress_tests[];

int mt_test_stuck_address(unsigned long volatile *bufa, size_t count);
int mt_test_random_value(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_xor_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_sub_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_mul_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_div_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_or_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_and_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_seqinc_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_solidbits_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_checkerboard_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_blockseq_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_walkbits0_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_walkbits1_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_bitspread_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_bitflip_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);

int mt_test_stress_memcpy(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_stress_memset(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int mt_test_stress_memcmp(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);

#ifdef __cplusplus
}
#endif
