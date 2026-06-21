#include "mt_tests.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long ul;
typedef unsigned long volatile ulv;

#define rand32() ((unsigned int)rand() | ((unsigned int)rand() << 16))

#if (ULONG_MAX == 4294967295UL)
#define rand_ul() rand32()
#define UL_ONEBITS 0xffffffff
#define UL_LEN 32
#define CHECKERBOARD1 0x55555555
#define CHECKERBOARD2 0xaaaaaaaa
#define UL_BYTE(x) ((x | x << 8 | x << 16 | x << 24))
#else
#define rand64() (((ul)rand32()) << 32 | ((ul)rand32()))
#define rand_ul() rand64()
#define UL_ONEBITS 0xffffffffffffffffUL
#define UL_LEN 64
#define CHECKERBOARD1 0x5555555555555555
#define CHECKERBOARD2 0xaaaaaaaaaaaaaaaa
#define UL_BYTE(x) (((ul)x | (ul)x << 8 | (ul)x << 16 | (ul)x << 24 | (ul)x << 32 | (ul)x << 40 | (ul)x << 48 | (ul)x << 56))
#endif

#define ONE 0x00000001L

unsigned short mt_dividend = 1;
volatile int mt_abort = 0;

struct mt_test mt_tests[] = {
    { "Random Value", mt_test_random_value },
    { "Compare XOR", mt_test_xor_comparison },
    { "Compare SUB", mt_test_sub_comparison },
    { "Compare MUL", mt_test_mul_comparison },
    { "Compare DIV", mt_test_div_comparison },
    { "Compare OR", mt_test_or_comparison },
    { "Compare AND", mt_test_and_comparison },
    { "Sequential Increment", mt_test_seqinc_comparison },
    { "Solid Bits", mt_test_solidbits_comparison },
    { "Block Sequential", mt_test_blockseq_comparison },
    { "Checkerboard", mt_test_checkerboard_comparison },
    { "Bit Spread", mt_test_bitspread_comparison },
    { "Bit Flip", mt_test_bitflip_comparison },
    { "Walking Ones", mt_test_walkbits0_comparison },
    { "Walking Zeroes", mt_test_walkbits1_comparison },
    { NULL, NULL }
};

struct mt_test mt_stress_tests[] = {
    { "Stress memcpy x128", mt_test_stress_memcpy },
    { "Stress memset x128", mt_test_stress_memset },
    { "Stress memcmp x32", mt_test_stress_memcmp },
    { NULL, NULL }
};

static int compare_regions(ulv *bufa, ulv *bufb, size_t count) {
    int r = 0;
    size_t i;
    ulv *p1 = bufa;
    ulv *p2 = bufb;

    for (i = 0; i < count; i++, p1++, p2++) {
        if (*p1 != *p2)
            r = -1;
    }
    return r;
}

int mt_test_stuck_address(ulv *bufa, size_t count) {
    ulv *p1 = bufa;
    unsigned int j;
    size_t i;

    for (j = 0; j < (16 / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        p1 = (ulv *)bufa;
        for (i = 0; i < count; i++) {
            *p1 = ((j + i) % 2) == 0 ? (ul)p1 : ~((ul)p1);
            *p1++;
        }
        p1 = (ulv *)bufa;
        for (i = 0; i < count; i++, p1++) {
            if (*p1 != (((j + i) % 2) == 0 ? (ul)p1 : ~((ul)p1)))
                return -1;
        }
    }
    return 0;
}

int mt_test_stress_memcpy(ulv *bufa, ulv *bufb, size_t count) {
    unsigned int j;

    int q = rand_ul();
    memset((void *)bufa, q, count * sizeof(ul));
    memset((void *)bufb, q, count * sizeof(ul));

    for (j = 0; j < 128; j++)
        memcpy((void *)bufa, (void *)bufb, count * sizeof(ul));

    return memcmp((void *)bufa, (void *)bufb, count * sizeof(ul));
}

int mt_test_stress_memset(ulv *bufa, ulv *bufb, size_t count) {
    unsigned int j;

    for (j = 0; j < 128; j++) {
        int q = rand_ul();
        memset((void *)bufa, q, count * sizeof(ul));
        memset((void *)bufb, q, count * sizeof(ul));
    }
    return memcmp((void *)bufa, (void *)bufb, count * sizeof(ul));
}

int mt_test_stress_memcmp(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;

    int q = rand_ul();
    memset((void *)bufa, q, count * sizeof(ul));
    memset((void *)bufb, q, count * sizeof(ul));

    int rc = 0;

    for (j = 0; j < 32; j++) {
        *p1++ = *p2++ = rand_ul();
        rc = memcmp((void *)bufa, (void *)bufb, count * sizeof(ul));
        if (rc)
            return rc;
    }
    return rc;
}

int mt_test_random_value(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;

    for (i = 0; i < count; i++)
        *p1++ = *p2++ = rand_ul();

    return compare_regions(bufa, bufb, count);
}

int mt_test_xor_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ ^= q;
        *p2++ ^= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_sub_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ -= q;
        *p2++ -= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_mul_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ *= q;
        *p2++ *= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_div_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        if (!q)
            q++;
        *p1++ /= q;
        *p2++ /= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_or_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ |= q;
        *p2++ |= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_and_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ &= q;
        *p2++ &= q;
    }
    return compare_regions(bufa, bufb, count);
}

int mt_test_seqinc_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++)
        *p1++ = *p2++ = (i + q);

    return compare_regions(bufa, bufb, count);
}

int mt_test_solidbits_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    ul q;
    size_t i;

    for (j = 0; j < (64 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        q = (j % 2) == 0 ? UL_ONEBITS : 0;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++)
            *p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_checkerboard_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    ul q;
    size_t i;

    for (j = 0; j < (64 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        q = (j % 2) == 0 ? CHECKERBOARD1 : CHECKERBOARD2;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++)
            *p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_blockseq_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;

    for (j = 0; j < (64 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++)
            *p1++ = *p2++ = (ul)UL_BYTE(j);
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_walkbits0_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;

    for (j = 0; j < (UL_LEN * 2 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++) {
            if (j < UL_LEN)
                *p1++ = *p2++ = ONE << j;
            else
                *p1++ = *p2++ = ONE << (UL_LEN * 2 - j - 1);
        }
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_walkbits1_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;

    for (j = 0; j < (UL_LEN * 2 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++) {
            if (j < UL_LEN)
                *p1++ = *p2++ = UL_ONEBITS ^ (ONE << j);
            else
                *p1++ = *p2++ = UL_ONEBITS ^ (ONE << (UL_LEN * 2 - j - 1));
        }
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_bitspread_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;

    for (j = 0; j < (UL_LEN * 2 / mt_dividend / mt_dividend); j++) {
        if (mt_abort)
            return 0;
        p1 = (ulv *)bufa;
        p2 = (ulv *)bufb;
        for (i = 0; i < count; i++) {
            if (j < UL_LEN)
                *p1++ = *p2++ = (i % 2 == 0)
                    ? (ONE << j) | (ONE << (j + 2))
                    : UL_ONEBITS ^ ((ONE << j) | (ONE << (j + 2)));
            else
                *p1++ = *p2++ = (i % 2 == 0)
                    ? (ONE << (UL_LEN * 2 - 1 - j)) | (ONE << (UL_LEN * 2 + 1 - j))
                    : UL_ONEBITS ^ (ONE << (UL_LEN * 2 - 1 - j) | (ONE << (UL_LEN * 2 + 1 - j)));
        }
        if (compare_regions(bufa, bufb, count))
            return -1;
    }
    return 0;
}

int mt_test_bitflip_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j, k;
    ul q;
    size_t i;

    for (k = 0; k < (UL_LEN / mt_dividend / mt_dividend); k++) {
        if (mt_abort)
            return 0;
        q = ONE << k;
        for (j = 0; j < 8; j++) {
            q = ~q;
            p1 = (ulv *)bufa;
            p2 = (ulv *)bufb;
            for (i = 0; i < count; i++)
                *p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
            if (compare_regions(bufa, bufb, count))
                return -1;
        }
    }
    return 0;
}
