#ifndef XASH_PSA_CONFIG_H
#define XASH_PSA_CONFIG_H

#if (defined( _WIN32 ) && !defined( _WIN64 )) || defined( __vita__ ) || defined( __SWITCH__ )
/* WinXP, PSVita and NSwitch use entropy paths upstream doesn't cover.
   compat.c provides mbedtls_platform_get_entropy() for all three. */
#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY
#endif

#if defined( __vita__ ) || defined( __SWITCH__ )
/* Upstream has no Vita/NSW support; compat.c fills in */
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#endif

/* oldmac-mbedtls-ms-time (task#6): route mbedtls_ms_time() through the engine's
   own Platform_DoubleTime() on Apple, unconditionally.

   This was version-scoped to "macOS before 10.12", on the reasoning that older
   systems have no clock_gettime() while a modern build should keep it. That
   reasoning was wrong, and the scoping only ever worked by accident.

   In THIS configuration mbedTLS never provides mbedtls_ms_time() on any
   platform. Its implementation in tf-psa-crypto/platform/platform_util.c sits
   behind

       #if defined(MBEDTLS_HAVE_TIME) && !defined(MBEDTLS_PLATFORM_MS_TIME_ALT)

   and MBEDTLS_HAVE_TIME is not defined anywhere in the config we build with.
   So the only thing that has ever supplied the symbol is compat.c, reached via
   MBEDTLS_PLATFORM_MS_TIME_ALT.

   Every slice built until now targets an OS older than 10.12, so the ALT branch
   was always taken and the gap never showed. The arm64 slice is the first build
   with a version-min above 10.12: the macro was correctly NOT defined, compat.c
   compiled itself out, mbedTLS still did not provide the function, and libxash
   failed to link with

       Undefined symbols for architecture arm64: "_mbedtls_ms_time",
         referenced from: _psa_random_internal_generate

   Verified by nm: platform_util.c.o contains no _mbedtls_ms_time at all.

   Platform_DoubleTime() is the clock the rest of the engine already uses, so
   taking it on every Apple target is both simpler and more consistent than a
   version test that silently depended on always being true. */
#if defined( __APPLE__ )
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#endif

#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_HMAC_DRBG_C
#undef MBEDTLS_LMS_C
#undef MBEDTLS_NIST_KW_C
#undef MBEDTLS_PKCS5_C

#undef PSA_WANT_ALG_CCM
#undef PSA_WANT_ALG_CCM_STAR_NO_TAG
#undef PSA_WANT_ALG_CBC_NO_PADDING
#undef PSA_WANT_ALG_CBC_PKCS7
#undef PSA_WANT_ALG_CFB
#undef PSA_WANT_ALG_CMAC
#undef PSA_WANT_ALG_CTR
#undef PSA_WANT_ALG_ECB_NO_PADDING
#undef PSA_WANT_ALG_OFB
#undef PSA_WANT_ALG_STREAM_CIPHER
#undef PSA_WANT_ALG_JPAKE
#undef PSA_WANT_ALG_TLS12_ECJPAKE_TO_PMS
#undef PSA_WANT_ALG_TLS12_PSK_TO_MS
#undef PSA_WANT_ALG_FFDH
#undef PSA_WANT_ALG_PBKDF2_HMAC
#undef PSA_WANT_ALG_PBKDF2_AES_CMAC_PRF_128
#undef PSA_WANT_KEY_TYPE_PASSWORD
#undef PSA_WANT_KEY_TYPE_PASSWORD_HASH
#undef PSA_WANT_ALG_SHA3_224
#undef PSA_WANT_ALG_SHA3_256
#undef PSA_WANT_ALG_SHA3_384
#undef PSA_WANT_ALG_SHA3_512
#undef PSA_WANT_ALG_SHAKE128
#undef PSA_WANT_ALG_SHAKE256
#undef PSA_WANT_ALG_SHA_224
#undef PSA_WANT_ALG_SHA_1
#undef PSA_WANT_ALG_MD5
#undef PSA_WANT_ALG_RIPEMD160
#undef PSA_WANT_KEY_TYPE_ARIA
#undef PSA_WANT_KEY_TYPE_CAMELLIA
#undef PSA_WANT_ECC_BRAINPOOL_P_R1_256
#undef PSA_WANT_ECC_BRAINPOOL_P_R1_384
#undef PSA_WANT_ECC_BRAINPOOL_P_R1_512
#undef PSA_WANT_ECC_SECP_K1_256
#undef PSA_WANT_ECC_MONTGOMERY_448
#undef PSA_WANT_ECC_SECP_R1_521
#undef PSA_WANT_DH_RFC7919_2048
#undef PSA_WANT_DH_RFC7919_3072
#undef PSA_WANT_DH_RFC7919_4096
#undef PSA_WANT_DH_RFC7919_6144
#undef PSA_WANT_DH_RFC7919_8192
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_BASIC
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_EXPORT
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_GENERATE
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_IMPORT
#undef PSA_WANT_KEY_TYPE_DH_PUBLIC_KEY
#undef PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT
#undef PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT
#undef PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_EXPORT
#undef PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_GENERATE
#undef PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_IMPORT
#undef MBEDTLS_PEM_WRITE_C
#undef MBEDTLS_PK_WRITE_C
#undef MBEDTLS_PK_PARSE_EC_COMPRESSED
#undef MBEDTLS_PK_PARSE_EC_EXTENDED
#undef MBEDTLS_ASN1_WRITE_C

/* AES-NI cannot be built for 32-bit x86 with a 2013-era clang, and would be
 * dead code there anyway.
 *
 * MBEDTLS_AESNI_C is defined by tf-psa-crypto/include/psa/crypto_config.h, so it
 * has to be switched off HERE and not in xash_mbedtls_config.h: aesni.c lives
 * under tf-psa-crypto, and that tree reads TF_PSA_CRYPTO_USER_CONFIG_FILE, which
 * is this file.
 *
 * aesni.h decides how to implement AES-NI like this:
 *
 *     For 32-bit, we only support intrinsics
 *     #if defined(MBEDTLS_ARCH_IS_X86) && (defined(__GNUC__) || defined(__clang__))
 *     #define MBEDTLS_AESNI_HAVE_INTRINSICS
 *     #endif
 *
 * On 32-bit that branch asks neither for __AES__ / __PCLMUL__ nor for a compiler
 * version, unlike the 64-bit path just above it. Apple clang 4.2 (LLVM 3.2) is
 * old enough that its 32-bit x86 AES-NI intrinsics do not work: __m128i comes
 * back as an incompatible type and the build dies with 20 errors in aesni.c.
 * The assembly fallback is guarded on MBEDTLS_ARCH_IS_X64, so there is nothing
 * to fall back to and aesni.h raises
 *     #error "MBEDTLS_AESNI_C defined, but neither intrinsics nor assembly available"
 * which is why MBEDTLS_AESNI_C itself has to go rather than just the intrinsics.
 *
 * Nothing is lost. The i386 slice exists for the 2006 Core Solo and Core Duo
 * Macs, the only Intel Macs with no 64-bit mode. AES-NI arrived with Westmere in
 * 2010, so no CPU that can ever run this slice has the instructions. mbedTLS
 * falls back to its portable C AES, and HTTPS keeps working.
 *
 * x86_64 is untouched and keeps AES-NI.
 */
#if defined(__i386__)
#undef MBEDTLS_AESNI_C
#endif

#endif /* XASH_PSA_CONFIG_H */
