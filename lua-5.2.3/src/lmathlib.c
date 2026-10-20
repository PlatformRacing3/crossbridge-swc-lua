/*
** $Id: lmathlib.c,v 1.83.1.1 2013/04/12 18:48:47 roberto Exp $
** Standard mathematical library
** See Copyright Notice in lua.h
*/


#include <stdlib.h>
#include <math.h>

#define lmathlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#undef PI
#define PI	((lua_Number)(3.1415926535897932384626433832795))
#define RADIANS_PER_DEGREE	((lua_Number)(PI/180.0))



static int math_abs (lua_State *L) {
  lua_pushnumber(L, l_mathop(fabs)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_sin (lua_State *L) {
  lua_pushnumber(L, l_mathop(sin)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_sinh (lua_State *L) {
  lua_pushnumber(L, l_mathop(sinh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_cos (lua_State *L) {
  lua_pushnumber(L, l_mathop(cos)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_cosh (lua_State *L) {
  lua_pushnumber(L, l_mathop(cosh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_tan (lua_State *L) {
  lua_pushnumber(L, l_mathop(tan)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_tanh (lua_State *L) {
  lua_pushnumber(L, l_mathop(tanh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_asin (lua_State *L) {
  lua_pushnumber(L, l_mathop(asin)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_acos (lua_State *L) {
  lua_pushnumber(L, l_mathop(acos)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_atan (lua_State *L) {
  lua_pushnumber(L, l_mathop(atan)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_atan2 (lua_State *L) {
  lua_pushnumber(L, l_mathop(atan2)(luaL_checknumber(L, 1),
                                luaL_checknumber(L, 2)));
  return 1;
}

static int math_ceil (lua_State *L) {
  lua_pushnumber(L, l_mathop(ceil)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_floor (lua_State *L) {
  lua_pushnumber(L, l_mathop(floor)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_fmod (lua_State *L) {
  lua_pushnumber(L, l_mathop(fmod)(luaL_checknumber(L, 1),
                               luaL_checknumber(L, 2)));
  return 1;
}

static int math_modf (lua_State *L) {
  lua_Number ip;
  lua_Number fp = l_mathop(modf)(luaL_checknumber(L, 1), &ip);
  lua_pushnumber(L, ip);
  lua_pushnumber(L, fp);
  return 2;
}

static int math_sqrt (lua_State *L) {
  lua_pushnumber(L, l_mathop(sqrt)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_pow (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number y = luaL_checknumber(L, 2);
  lua_pushnumber(L, l_mathop(pow)(x, y));
  return 1;
}

static int math_log (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number res;
  if (lua_isnoneornil(L, 2))
    res = l_mathop(log)(x);
  else {
    lua_Number base = luaL_checknumber(L, 2);
    if (base == (lua_Number)10.0) res = l_mathop(log10)(x);
    else res = l_mathop(log)(x)/l_mathop(log)(base);
  }
  lua_pushnumber(L, res);
  return 1;
}

#if defined(LUA_COMPAT_LOG10)
static int math_log10 (lua_State *L) {
  lua_pushnumber(L, l_mathop(log10)(luaL_checknumber(L, 1)));
  return 1;
}
#endif

static int math_exp (lua_State *L) {
  lua_pushnumber(L, l_mathop(exp)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_deg (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1)/RADIANS_PER_DEGREE);
  return 1;
}

static int math_rad (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1)*RADIANS_PER_DEGREE);
  return 1;
}

static int math_frexp (lua_State *L) {
  int e;
  lua_pushnumber(L, l_mathop(frexp)(luaL_checknumber(L, 1), &e));
  lua_pushinteger(L, e);
  return 2;
}

static int math_ldexp (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  int ep = luaL_checkint(L, 2);
  lua_pushnumber(L, l_mathop(ldexp)(x, ep));
  return 1;
}



static int math_min (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  lua_Number dmin = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    lua_Number d = luaL_checknumber(L, i);
    if (d < dmin)
      dmin = d;
  }
  lua_pushnumber(L, dmin);
  return 1;
}


static int math_max (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  lua_Number dmax = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    lua_Number d = luaL_checknumber(L, i);
    if (d > dmax)
      dmax = d;
  }
  lua_pushnumber(L, dmax);
  return 1;
}


static int math_random (lua_State *L) {
  /* the `%' avoids the (rare) case of r==1, and is needed also because on
     some systems (SunOS!) `rand()' may return a value larger than RAND_MAX */
  lua_Number r = (lua_Number)(rand()%RAND_MAX) / (lua_Number)RAND_MAX;
  switch (lua_gettop(L)) {  /* check number of arguments */
    case 0: {  /* no arguments */
      lua_pushnumber(L, r);  /* Number between 0 and 1 */
      break;
    }
    case 1: {  /* only upper limit */
      lua_Number u = luaL_checknumber(L, 1);
      luaL_argcheck(L, (lua_Number)1.0 <= u, 1, "interval is empty");
      lua_pushnumber(L, l_mathop(floor)(r*u) + (lua_Number)(1.0));  /* [1, u] */
      break;
    }
    case 2: {  /* lower and upper limits */
      lua_Number l = luaL_checknumber(L, 1);
      lua_Number u = luaL_checknumber(L, 2);
      luaL_argcheck(L, l <= u, 2, "interval is empty");
      lua_pushnumber(L, l_mathop(floor)(r*(u-l+1)) + l);  /* [l, u] */
      break;
    }
    default: return luaL_error(L, "wrong number of arguments");
  }
  return 1;
}


static int math_randomseed (lua_State *L) {
  srand(luaL_checkunsigned(L, 1));
  (void)rand(); /* discard first value to avoid undesirable correlations */
  return 0;
}

static int math_clamp (lua_State *L) {
  lua_Number val = luaL_checknumber(L, 1);
  lua_Number min = luaL_checknumber(L, 2);
  lua_Number max = luaL_checknumber(L, 3);
  luaL_argcheck(L, min <= max, 2, "minimum must be less than or equal to maximum");
  if (val < min) {
    lua_pushnumber(L,min);
  } else if (val > max) {
    lua_pushnumber(L,max);
  } else {
    lua_pushnumber(L,val);
  }
  return 1;  
}

static int math_sign (lua_State *L) {
  lua_Number val = luaL_checknumber(L, 1);
  if (val < 0) {
    lua_pushnumber(L, -1);
  } else if (val > 0) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnumber(L, 0);
  }
  return 1;
}

static int math_round (lua_State *L) {
  lua_Number val = luaL_checknumber(L, 1);
  if (val < 0.0) {
    lua_pushnumber(L,l_mathop(ceil)(val - 0.5));
  } else {
    lua_pushnumber(L,l_mathop(floor)(val + 0.5));
  }
  return 1;
}

static int math_lerp (lua_State *L) {
  lua_Number a = luaL_checknumber(L, 1);
  lua_Number b = luaL_checknumber(L, 2);
  lua_Number t = luaL_checknumber(L, 3);
	
  lua_Number r;
  if (t == 1.0) {
    r = b;
  } else {
    r = a + (b - a) * t;
  }
  lua_pushnumber(L,r);
	
  return 1;
}

static int math_isnan (lua_State *L) {
  lua_Number val = luaL_checknumber(L, 1);
	
  lua_pushboolean(L, isnan(val));
	
  return 1;
}

// small, fast 64 bit hash function (version 2), with streaming api.
//
// https://github.com/N-R-K/ChibiHash
//
// Usage
// =====
// ChibiHash64Ctx ctx = chibihash64_init(seed);
// chibihash64_append(&ctx, "hello, ", 7);
// chibihash64_append(&ctx, "world!", 6);
// uint64_t hash = chibihash64_finish(&ctx);
//
// This is free and unencumbered software released into the public domain.
// For more information, please refer to <https://unlicense.org/>

typedef unsigned long long int uint64_t;
typedef unsigned char uint8_t;

typedef struct {
	uint64_t  h[4];
	uint8_t   buf[32];
	int       buf_len;
	int       total_len;
	uint64_t  seed;
} ChibiHash64Ctx;

static inline uint64_t chibihash64__load32le(const unsigned char *p)
{
	return (uint64_t)p[0] <<  0 | (uint64_t)p[1] <<  8 |
	       (uint64_t)p[2] << 16 | (uint64_t)p[3] << 24;
}
static inline uint64_t chibihash64__load64le(const unsigned char *p)
{
	return chibihash64__load32le(p) | (chibihash64__load32le(p+4) << 32);
}
static inline uint64_t chibihash64__rotl(uint64_t x, int n)
{
	return (x << n) | (x >> (-n & 63));
}

static inline void
chibihash64__stream_block(uint64_t h[4], const unsigned char *p)
{
	const uint64_t K = 0x2B7E151628AED2A7ULL;
	int i = 0;
	for (i = 0; i < 4; ++i) {
		uint64_t stripe = chibihash64__load64le(p);
		h[i] = (stripe + h[i]) * K;
		h[(i+1)&3] += chibihash64__rotl(stripe, 27);
		p += 8;
	}
}

static inline ChibiHash64Ctx
chibihash64_init(uint64_t seed)
{
	const uint64_t K = 0x2B7E151628AED2A7ULL; // digits of e
	uint64_t seed2 = chibihash64__rotl(seed-K, 15) + chibihash64__rotl(seed-K, 47);
	ChibiHash64Ctx ctx = {0};
	ctx.h[0] = seed;
	ctx.h[1] = seed+K;
	ctx.h[2] = seed2;
	ctx.h[3] = seed2+(K*K^K);
	ctx.seed = seed;
	return ctx;
}

static inline void
chibihash64_append(ChibiHash64Ctx *ctx, void *keyIn, ptrdiff_t len)
{
	const unsigned char *p = (const unsigned char *)keyIn;
	ptrdiff_t l = len;

	ctx->total_len += len;

	for (; l > 0 && ctx->buf_len < 32; --l, ++p) {
		ctx->buf[ctx->buf_len++] = *p;
	}

	if (ctx->buf_len == 32) {
		chibihash64__stream_block(ctx->h, ctx->buf);
		ctx->buf_len = 0;
	}

	for (; l >= 32; l -= 32, p += 32) {
		chibihash64__stream_block(ctx->h, p);
	}

	for (; l > 0; --l, ++p) {
		ctx->buf[ctx->buf_len++] = *p;
	}
}

static inline uint64_t
chibihash64_finish(ChibiHash64Ctx *ctx)
{
	const uint64_t K = 0x2B7E151628AED2A7ULL;
	const uint8_t *p = ctx->buf;
	ptrdiff_t l = ctx->buf_len;
	uint64_t h[4] = { ctx->h[0], ctx->h[1], ctx->h[2], ctx->h[3] };

	for (; l >= 8; l -= 8, p += 8) {
		h[0] ^= chibihash64__load32le(p+0); h[0] *= K;
		h[1] ^= chibihash64__load32le(p+4); h[1] *= K;
	}

	if (l >= 4) {
		h[2] ^= chibihash64__load32le(p);
		h[3] ^= chibihash64__load32le(p + l - 4);
	} else if (l > 0) {
		h[2] ^= p[0];
		h[3] ^= p[l/2] | ((uint64_t)p[l-1] << 8);
	}

	h[0] += chibihash64__rotl(h[2] * K, 31) ^ (h[2] >> 31);
	h[1] += chibihash64__rotl(h[3] * K, 31) ^ (h[3] >> 31);
	h[0] *= K; h[0] ^= h[0] >> 31;
	h[1] += h[0];

	uint64_t x = (uint64_t)ctx->total_len * K;
	x ^= chibihash64__rotl(x, 29);
	x += ctx->seed;
	x ^= h[1];

	x ^= chibihash64__rotl(x, 15) ^ chibihash64__rotl(x, 42);
	x *= K;
	x ^= chibihash64__rotl(x, 13) ^ chibihash64__rotl(x, 31);

	return x;
}

//  ***  HASH IMPLEMENTATION END  ***  //

typedef union {double d; uint64_t u64;} uint64_double; // For reinterpreting values between uint64_t and double.

static int math_hash (lua_State *L) {
	ChibiHash64Ctx ctx = chibihash64_init(0x8FE89EC4F0716462ULL); // random number. Idk this just needs to be constant.
  // chibihash64_append(&ctx, "hello, ", 7);
  // chibihash64_append(&ctx, "world!", 6);
  // uint64_t hash = chibihash64_finish(&ctx);
  int n = lua_gettop(L);
	int i = 0;
	for (i = 0; i < n; i++) {
		lua_Number v = luaL_checknumber(L, i + 1);
		chibihash64_append(&ctx, &v, 8);
	}
	uint64_double result;
	result.u64 = ((chibihash64_finish(&ctx) >>12) | 0x3ff0000000000000ULL);
	lua_pushnumber(L, result.d - 1.0);
  return 1;
}

static const luaL_Reg mathlib[] = {
  {"abs",   math_abs},
  {"acos",  math_acos},
  {"asin",  math_asin},
  {"atan2", math_atan2},
  {"atan",  math_atan},
  {"ceil",  math_ceil},
  {"cosh",   math_cosh},
  {"cos",   math_cos},
  {"deg",   math_deg},
  {"exp",   math_exp},
  {"floor", math_floor},
  {"fmod",   math_fmod},
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
#if defined(LUA_COMPAT_LOG10)
  {"log10", math_log10},
#endif
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",   math_modf},
  {"pow",   math_pow},
  {"rad",   math_rad},
  {"random",     math_random},
  {"randomseed", math_randomseed},
  {"sinh",   math_sinh},
  {"sin",   math_sin},
  {"sqrt",  math_sqrt},
  {"tanh",   math_tanh},
  {"tan",   math_tan},
  {"clamp", math_clamp},
  {"sign", math_sign},
  {"round", math_round},
  {"lerp", math_lerp},
  {"isnan", math_isnan},
	{"hash", math_hash},
  {NULL, NULL}
};


/*
** Open math library
*/
LUAMOD_API int luaopen_math (lua_State *L) {
  luaL_newlib(L, mathlib);
  lua_pushnumber(L, PI);
  lua_setfield(L, -2, "pi");
  lua_pushnumber(L, HUGE_VAL);
  lua_setfield(L, -2, "huge");
  lua_pushnumber(L, NAN);
  lua_setfield(L, -2, "nan");
  return 1;
}

