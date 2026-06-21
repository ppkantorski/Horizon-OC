#pragma once
#include <arm_neon.h>

struct vec2f {
    float x, y;

    inline vec2f() {
    }
    inline vec2f(float x_, float y_) : x(x_), y(y_) {
    }
};

struct vec3x4 {
    float32x4_t x, y, z;
};

inline float32x4_t dot(const vec3x4 &a, const vec3x4 &b) {
    float32x4_t acc = vmulq_f32(a.x, b.x);
    acc = vfmaq_f32(acc, a.y, b.y);
    acc = vfmaq_f32(acc, a.z, b.z);
    return acc;
}

inline vec3x4 normalize(vec3x4 v) {
    float32x4_t len2 = vfmaq_f32(vfmaq_f32(vmulq_f32(v.x, v.x), v.y, v.y), v.z, v.z);

    float32x4_t invLen = vrsqrteq_f32(len2);

    invLen = vmulq_f32(vrsqrtsq_f32(vmulq_f32(len2, invLen), invLen), invLen);

    return { vmulq_f32(v.x, invLen), vmulq_f32(v.y, invLen), vmulq_f32(v.z, invLen) };
}

inline vec3x4 reflect(const vec3x4 &v, const vec3x4 &n) {
    float32x4_t two_d = vmulq_n_f32(dot(v, n), 2.0f);
    vec3x4 r;
    r.x = vfmsq_f32(v.x, n.x, two_d);
    r.y = vfmsq_f32(v.y, n.y, two_d);
    r.z = vfmsq_f32(v.z, n.z, two_d);
    return r;
}

struct vec3f {
    float x, y, z;

    inline vec3f() {
    }
    inline vec3f(float v) : x(v), y(v), z(v) {
    }
    inline vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
    }
};

inline vec3f operator+(const vec3f &a, const vec3f &b) {
    return vec3f(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline vec3f operator-(const vec3f &a, const vec3f &b) {
    return vec3f(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline vec3f operator-(const vec3f &v) {
    return vec3f(-v.x, -v.y, -v.z);
}

inline vec3f &operator*=(vec3f &a, const vec3f &b) {
    a.x *= b.x;
    a.y *= b.y;
    a.z *= b.z;
    return a;
}

inline vec3f &operator*=(vec3f &a, float b) {
    a.x *= b;
    a.y *= b;
    a.z *= b;
    return a;
}

inline vec3f operator*(const vec3f &a, float b) {
    return vec3f(a.x * b, a.y * b, a.z * b);
}

inline vec3f operator*(float b, const vec3f &a) {
    return a * b;
}

inline vec3f operator*(const vec3f &a, const vec3f &b) {
    return vec3f(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline vec3f &operator+=(vec3f &a, const vec3f &b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline vec3f operator/(const vec3f &a, float b) {
    float inv = 1.0f / b;
    return vec3f(a.x * inv, a.y * inv, a.z * inv);
}

inline vec3f &operator/=(vec3f &a, float b) {
    float inv = 1.0f / b;
    a.x *= inv;
    a.y *= inv;
    a.z *= inv;
    return a;
}

inline float dot(const vec3f &a, const vec3f &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vec3f normalize(const vec3f &v) {
    float len2 = dot(v, v);
    float invLen = 1.0f / sqrtf(len2 + 1e-20f);
    return v * invLen;
}

inline vec3f cross(const vec3f &a, const vec3f &b) {
    return vec3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline float32x4_t fastRecip(float32x4_t x) {
    float32x4_t r = vrecpeq_f32(x);
    r = vmulq_f32(vrecpsq_f32(x, r), r);
    r = vmulq_f32(vrecpsq_f32(x, r), r);
    return r;
}

static inline float32x4_t fastRsqrt(float32x4_t x) {
    float32x4_t r = vrsqrteq_f32(x);
    r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(x, r), r));
    r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(x, r), r));
    return r;
}

static inline vec3x4 normalizeFast(const vec3x4 &v) {
    float32x4_t len2 = vfmaq_f32(vmulq_f32(v.z, v.z), v.y, v.y);

    len2 = vfmaq_f32(len2, v.x, v.x);

    float32x4_t invLen = fastRsqrt(len2);

    vec3x4 out;
    out.x = vmulq_f32(v.x, invLen);
    out.y = vmulq_f32(v.y, invLen);
    out.z = vmulq_f32(v.z, invLen);
    return out;
}

inline uint32x4_t xorshift4(uint32x4_t &s) {
    s = veorq_u32(s, vshlq_n_u32(s, 13));
    s = veorq_u32(s, vshrq_n_u32(s, 17));
    s = veorq_u32(s, vshlq_n_u32(s, 5));
    return s;
}

inline float32x4_t toFloat01(uint32x4_t x) {
    uint32x4_t m = vandq_u32(x, vdupq_n_u32(0xFFFFFF));
    return vmulq_n_f32(vcvtq_f32_u32(m), 1.0f / float(0xFFFFFF));
}

inline float32x4_t sin4(float32x4_t phi) {

    const float32x4_t pi = vdupq_n_f32(3.14159265f);
    const float32x4_t pi_2 = vdupq_n_f32(1.57079633f);
    // const float32x4_t two_pi = vdupq_n_f32(6.28318531f);

    uint32x4_t neg = vcgeq_f32(phi, pi);
    float32x4_t xr = vbslq_f32(neg, vsubq_f32(phi, pi), phi);

    uint32x4_t fold = vcgtq_f32(xr, pi_2);
    xr = vbslq_f32(fold, vsubq_f32(pi, xr), xr);

    float32x4_t x2 = vmulq_f32(xr, xr);
    float32x4_t r = vfmsq_f32(vdupq_n_f32(1.0f / 120.0f), x2, vdupq_n_f32(1.0f / 5040.0f));
    r = vfmsq_f32(vdupq_n_f32(1.0f / 6.0f), x2, r);
    r = vfmsq_f32(vdupq_n_f32(1.0f), x2, r);
    r = vmulq_f32(xr, r);

    uint32x4_t signBit = vshlq_n_u32(neg, 31);
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(r), signBit));
}

inline float32x4_t cos4(float32x4_t phi) {

    float32x4_t shifted = vaddq_f32(phi, vdupq_n_f32(1.57079633f));
    float32x4_t two_pi = vdupq_n_f32(6.28318531f);
    uint32x4_t wrap = vcgeq_f32(shifted, two_pi);
    shifted = vbslq_f32(wrap, vsubq_f32(shifted, two_pi), shifted);
    return sin4(shifted);
}

inline void buildONB4(const vec3x4 &n, vec3x4 &t, vec3x4 &b) {
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t neg = vdupq_n_f32(-1.0f);

    uint32x4_t pos = vcgeq_f32(n.z, vdupq_n_f32(0.0f));
    float32x4_t sgn = vbslq_f32(pos, one, neg);

    float32x4_t denom = vaddq_f32(sgn, n.z);
    float32x4_t r = vrecpeq_f32(denom);
    r = vmulq_f32(vrecpsq_f32(denom, r), r);
    r = vmulq_f32(vrecpsq_f32(denom, r), r);
    float32x4_t a = vnegq_f32(r);

    float32x4_t bc = vmulq_f32(vmulq_f32(n.x, n.y), a);

    t.x = vaddq_f32(one, vmulq_f32(sgn, vmulq_f32(vmulq_f32(n.x, n.y), a)));
    t.y = vmulq_f32(sgn, bc);
    t.z = vnegq_f32(vmulq_f32(sgn, n.x));

    b.x = bc;
    b.y = vaddq_f32(sgn, vmulq_f32(vmulq_f32(n.y, n.y), a));
    b.z = vnegq_f32(n.y);
}

inline vec3x4 cosineSampleHemisphere4(const vec3x4 &n, uint32x4_t &rng) {

    float32x4_t u1 = toFloat01(xorshift4(rng));
    float32x4_t u2 = toFloat01(xorshift4(rng));

    float32x4_t r = vsqrtq_f32(u1);
    float32x4_t phi = vmulq_n_f32(u2, 6.28318531f);

    float32x4_t lx = vmulq_f32(r, cos4(phi));
    float32x4_t ly = vmulq_f32(r, sin4(phi));
    float32x4_t lz = vsqrtq_f32(vmaxq_f32(vsubq_f32(vdupq_n_f32(1.0f), u1), vdupq_n_f32(0.0)));

    vec3x4 t, b;
    buildONB4(n, t, b);

    vec3x4 d;
    d.x = vaddq_f32(vaddq_f32(vmulq_f32(lx, t.x), vmulq_f32(ly, b.x)), vmulq_f32(lz, n.x));
    d.y = vaddq_f32(vaddq_f32(vmulq_f32(lx, t.y), vmulq_f32(ly, b.y)), vmulq_f32(lz, n.y));
    d.z = vaddq_f32(vaddq_f32(vmulq_f32(lx, t.z), vmulq_f32(ly, b.z)), vmulq_f32(lz, n.z));
    return d;
}

inline float neon_dot3(float32x4_t a, float32x4_t b) {
    float32x4_t mul = vmulq_f32(a, b);
    float32x2_t lo = vget_low_f32(mul);
    float32x2_t hi = vget_high_f32(mul);
    float32x2_t sum = vpadd_f32(lo, lo);
    return vget_lane_f32(vadd_f32(sum, hi), 0);
}

inline float32x4_t neon_cross3(float32x4_t a, float32x4_t b) {
    float32x4_t a_yzx = __builtin_shufflevector(a, a, 1, 2, 0, 3);
    float32x4_t a_zxy = __builtin_shufflevector(a, a, 2, 0, 1, 3);
    float32x4_t b_yzx = __builtin_shufflevector(b, b, 1, 2, 0, 3);
    float32x4_t b_zxy = __builtin_shufflevector(b, b, 2, 0, 1, 3);
    return vsubq_f32(vmulq_f32(a_yzx, b_zxy), vmulq_f32(a_zxy, b_yzx));
}

inline float32x4_t neon_normalize3(float32x4_t v) {
    float32x2_t lenSq = vdup_n_f32(neon_dot3(v, v));
    float32x2_t est = vrsqrte_f32(lenSq);
    est = vmul_f32(est, vrsqrts_f32(vmul_f32(lenSq, est), est));
    return vmulq_f32(v, vcombine_f32(est, est));
}

inline void neon_store3(float *p, float32x4_t v) {
    vst1q_lane_f32(p + 0, v, 0);
    vst1q_lane_f32(p + 1, v, 1);
    vst1q_lane_f32(p + 2, v, 2);
}

inline float32x4_t vpermute(float32x4_t x) {
    float32x4_t v34 = vdupq_n_f32(34.0f);
    float32x4_t v1 = vdupq_n_f32(1.0f);
    float32x4_t v289 = vdupq_n_f32(289.0f);
    float32x4_t inv289 = vdupq_n_f32(1.0f / 289.0f);

    float32x4_t res = vmlaq_f32(v1, x, v34);
    res = vmulq_f32(res, x);

    float32x4_t quotient = vmulq_f32(res, inv289);

    float32x4_t floored = vrndmq_f32(quotient);
    res = vmlsq_f32(res, floored, v289);

    return res;
}

inline float32x4_t vtaylorInvSqrt(float32x4_t x) {
    float32x4_t c1 = vdupq_n_f32(1.79284291400159f);
    float32x4_t c2 = vdupq_n_f32(0.85373472095314f);
    return vmlsq_f32(c1, x, c2);
}

static inline void fastSinCos(float angle, float *s, float *c) {

    const float INV_TWO_PI = 0.15915494309f;
    const float TWO_PI = 6.28318530718f;
    // const float PI = 3.14159265359f;

    float x = angle - TWO_PI * floorf(angle * INV_TWO_PI + 0.5f);

    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x3;

    *s = x + x3 * (-0.16666667163f) + x5 * (0.00833333842f) + x7 * (-0.00019840680f);

    float x4 = x2 * x2;
    float x6 = x4 * x2;

    *c = 1.0f + x2 * (-0.49999997020f) + x4 * (0.04166664556f) + x6 * (-0.00138873165f);
}
