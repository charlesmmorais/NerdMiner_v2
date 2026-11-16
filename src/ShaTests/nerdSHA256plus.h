/************************************************************************************
*   written by: Bitmaker
*   based on: Blockstream Jade shaLib
*   thanks to @LarryBitcoin

*   Description:

*   NerdSha256plus is a custom C implementation of sha256d based on Blockstream Jade 
    code https://github.com/Blockstream/Jade

    The folowing file can be used on any ESP32 implementation using both cores

*************************************************************************************/
#ifndef nerdSHA256plus_H_
#define nerdSHA256plus_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fase 1 do plano de otimização: o core Arduino-ESP32 compila com -Os.
 * As funções quentes do SHA256d ganham com -O3 (alocação de registradores e
 * funnel-shift do Xtensa para ROTR). Desative com -D NERD_SHA_NO_O3 para
 * comparar o hashrate. */
#if defined(__GNUC__) && !defined(NERD_SHA_NO_O3)
#define NERD_HOT __attribute__((optimize("O3")))
#else
#define NERD_HOT
#endif


struct nerdSHA256_context {
    uint8_t buffer[64];
    uint32_t digest[8];
};

/* Calculate midstate */
NERD_HOT IRAM_ATTR void nerd_mids(uint32_t* digest, const uint8_t* dataIn);

NERD_HOT IRAM_ATTR bool nerd_sha256d(nerdSHA256_context* midstate, const uint8_t* dataIn, uint8_t* doubleHash);

NERD_HOT IRAM_ATTR void nerd_sha256_bake(const uint32_t* digest, const uint8_t* dataIn, uint32_t* bake);  //15 words
NERD_HOT IRAM_ATTR bool nerd_sha256d_baked(const uint32_t* digest, const uint8_t* dataIn, const uint32_t* bake, uint8_t* doubleHash);

void ByteReverseWords(uint32_t* out, const uint32_t* in, uint32_t byteCount);

#endif /* nerdSHA256plus_H_ */