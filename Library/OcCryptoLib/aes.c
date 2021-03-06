/*

This is an implementation of the AES algorithm, specifically CTR and CBC mode.
Block size can be chosen in aes.h - available choices are AES128, AES192, AES256.

The implementation is verified against the test vectors in:
  National Institute of Standards and Technology Special Publication 800-38A 2001 ED


NOTE:   String length must be evenly divisible by 16byte (str_len % 16 == 0)
        You should pad the end of the string with zeros if this is not the case.
        For AES192/256 the key size is proportionally larger.

*/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include "Aes.h"

//
// The number of columns comprising a state in AES. This is a CONSTant in AES. Value=4
//
#define Nb 4

#if defined(AES256) && (AES256 == 1)
#define Nk 8
#define Nr 14
#elif defined(AES192) && (AES192 == 1)
#define Nk 6
#define Nr 12
#else
//
// The number of 32 bit words in a key.
//
#define Nk 4
//
// The number of rounds in AES Cipher.
//
#define Nr 10
#endif

//
// jcallan@github points out that declaring Multiply as a function
// reduces code size considerably with the Keil ARM compiler.
// See this link for more information: https://github.com/kokke/tiny-AES-C/pull/3
//
#ifndef MULTIPLY_AS_A_FUNCTION
#define MULTIPLY_AS_A_FUNCTION 0
#endif


//
// state - array holding the intermediate results during decryption.
//
typedef UINT8 state_t[4][4];


//
// The lookup-tables are marked CONST so they can be placed in read-only storage instead of RAM
// The numbers below can be computed dynamically trading ROM for RAM -
// This can be useful in (embedded) bootloader applications, where ROM is often limited.
//
STATIC CONST UINT8 Sbox[256] = {
  //0     1    2      3     4    5     6     7      8    9     A      B    C     D     E     F
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

STATIC CONST UINT8 RsBox[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

//
// The round CONSTant word array, Rcon[i], contains the values given by
// x to the power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8)
//
STATIC CONST UINT8 Rcon[11] = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/*
 * Jordan Goulder points out in PR #12 (https://github.com/kokke/tiny-AES-C/pull/12),
 * that you can remove most of the elements in the Rcon array, because they are unused.
 *
 * From Wikipedia's article on the Rijndael key schedule @ https://en.wikipedia.org/wiki/Rijndael_key_schedule#Rcon
 *
 * "Only the first some of these CONSTants are actually used – up to rcon[10] for AES-128 (as 11 round keys are needed),
 *  up to rcon[8] for AES-192, up to rcon[7] for AES-256. rcon[0] is not used in AES algorithm."
 */


//
// Private functions:
//
#define GetSboxValue(num) (Sbox[(num)])
#define getSBoxInvert(num) (RsBox[(num)])

//
// This function produces Nb(Nr+1) round keys. The round keys are used in each
// round to decrypt the states.
//
STATIC
VOID
KeyExpansion (
  UINT8*        RoundKey,
  CONST UINT8*  Key
  )
{
  UINT32 Index, J, K;
  UINT8 TempA[4]; // Used for the column/row operations

  //
  // The first round key is the key itself.
  //
  for (Index = 0; Index < Nk; ++Index)
  {
    RoundKey[(Index * 4) + 0] = Key[(Index * 4) + 0];
    RoundKey[(Index * 4) + 1] = Key[(Index * 4) + 1];
    RoundKey[(Index * 4) + 2] = Key[(Index * 4) + 2];
    RoundKey[(Index * 4) + 3] = Key[(Index * 4) + 3];
  }

  //
  // All other round keys are found from the previous round keys.
  //
  for (Index = Nk; Index < Nb * (Nr + 1); ++Index)
  {
    {
      K = (Index - 1) * 4;
      TempA[0] = RoundKey[K + 0];
      TempA[1] = RoundKey[K + 1];
      TempA[2] = RoundKey[K + 2];
      TempA[3] = RoundKey[K + 3];

    }

    if (Index % Nk == 0)
    {
      //
      // This function shifts the 4 bytes in a word to the left once.
      // [a0,a1,a2,a3] becomes [a1,a2,a3,a0]
      //

      //
      // Function RotWord()
      //
      {
        K = TempA[0];
        TempA[0] = TempA[1];
        TempA[1] = TempA[2];
        TempA[2] = TempA[3];
        TempA[3] = (UINT8) K;
      }
      //
      // SubWord() is a function that takes a four-byte input word and
      // applies the S-box to each of the four bytes to produce an output word.
      //

      //
      // Function Subword()
      //
      {
        TempA[0] = GetSboxValue (TempA[0]);
        TempA[1] = GetSboxValue (TempA[1]);
        TempA[2] = GetSboxValue (TempA[2]);
        TempA[3] = GetSboxValue (TempA[3]);
      }

      TempA[0] = TempA[0] ^ Rcon[Index / Nk];
    }
#if defined(AES256) && (AES256 == 1)
    if (Index % Nk == 4)
    {
      //
      // Function Subword()
      //
      {
        TempA[0] = GetSboxValue (TempA[0]);
        TempA[1] = GetSboxValue (TempA[1]);
        TempA[2] = GetSboxValue (TempA[2]);
        TempA[3] = GetSboxValue (TempA[3]);
      }
    }
#endif
    J = Index * 4; K = (Index - Nk) * 4;
    RoundKey[J + 0] = RoundKey[K + 0] ^ TempA[0];
    RoundKey[J + 1] = RoundKey[K + 1] ^ TempA[1];
    RoundKey[J + 2] = RoundKey[K + 2] ^ TempA[2];
    RoundKey[J + 3] = RoundKey[K + 3] ^ TempA[3];
  }
}

VOID
AesInitCtx (
  struct AES_ctx*  Context,
  CONST UINT8*     Key
  )
{
  KeyExpansion (Context->RoundKey, Key);
}

#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
VOID
AesInitCtxIV (
  struct AES_ctx* ctx,
  CONST UINT8* key,
  CONST UINT8* iv
  )
{
  KeyExpansion (ctx->RoundKey, key);
  CopyMem (ctx->Iv, iv, AES_BLOCKLEN);
}

VOID
AesCtxSetIV (
  struct AES_ctx*  ctx,
  CONST UINT8*     iv
  )
{
  CopyMem (ctx->Iv, iv, AES_BLOCKLEN);
}
#endif

//
// This function adds the round key to state.
// The round key is added to the state by an XOR function.
//
STATIC 
VOID
AddRoundKey (
  UINT8 round,
  state_t* state,
  CONST UINT8* RoundKey
  )
{
  UINT8 i, j;
  for (i = 0; i < 4; ++i)
  {
    for (j = 0; j < 4; ++j)
    {
      (*state)[i][j] ^= RoundKey[(round * Nb * 4) + (i * Nb) + j];
    }
  }
}

//
// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
//
STATIC 
VOID
SubBytes (
  state_t* state
  )
{
  UINT8 i, j;
  for (i = 0; i < 4; ++i)
  {
    for (j = 0; j < 4; ++j)
    {
      (*state)[j][i] = GetSboxValue((*state)[j][i]);
    }
  }
}

//
// The ShiftRows() function shifts the rows in the state to the left.
// Each row is shifted with different offset.
// Offset = Row number. So the first row is not shifted.
//
STATIC 
VOID
ShiftRows (
  state_t* state
  )
{
  UINT8 temp;

  // Rotate first row 1 columns to left
  temp           = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  // Rotate second row 2 columns to left
  temp           = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;

  temp           = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  // Rotate third row 3 columns to left
  temp           = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

STATIC 
UINT8
xtime (
  UINT8 x
  )
{
  return (UINT8) (((UINT32) x << 1u) ^ ((((UINT32)x >> 7u) & 1u) * 0x1bu));
}

//
// MixColumns function mixes the columns of the state matrix
//
STATIC 
VOID
MixColumns (
  state_t* state
  )
{
  UINT8 i;
  UINT8 Tmp, Tm, t;
  for (i = 0; i < 4; ++i)
  {
    t   = (*state)[i][0];
    Tmp = (UINT8) ((UINT32) ((*state) [i][0]) ^ (UINT32) ((*state) [i][1])
          ^ (UINT32) ((*state) [i][2]) ^ (UINT32) ((*state) [i][3]));
    Tm  = (*state) [i][0] ^ (*state) [i][1];
    Tm = xtime (Tm);
    (*state) [i][0] ^= Tm ^ Tmp;
    Tm  = (*state)[i][1] ^ (*state)[i][2];
    Tm = xtime (Tm);
    (*state) [i][1] ^= Tm ^ Tmp;
    Tm  = (*state) [i][2] ^ (*state) [i][3];
    Tm = xtime (Tm);
    (*state) [i][2] ^= Tm ^ Tmp;
    Tm  = (*state)[i][3] ^ t ;
    Tm = xtime (Tm);
    (*state) [i][3] ^= Tm ^ Tmp ;
  }
}

//
// Multiply is used to multiply numbers in the field GF(2^8)
// Note: The last call to xtime() is unneeded, but often ends up generating a smaller binary
//       The compiler seems to be able to vectorize the operation better this way.
//       See https://github.com/kokke/tiny-AES-c/pull/34
//
#if MULTIPLY_AS_A_FUNCTION
STATIC 
UINT8
Multiply (
  UINT8 x,
  UINT8 y
  )
{
  return (((y & 1) * x) ^
          ((y >> 1 & 1) * xtime(x)) ^
          ((y >> 2 & 1) * xtime(xtime(x))) ^
          ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^
          ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x)))))); /* this last call to xtime() can be omitted */
}
#else
#define Multiply(x, y)                                \
      (  (((y) & 1u) * (x)) ^                              \
      (((y)>>1u & 1u) * xtime(x)) ^                       \
      (((y)>>2u & 1u) * xtime(xtime(x))) ^                \
      (((y)>>3u & 1u) * xtime(xtime(xtime(x)))) ^         \
      (((y)>>4u & 1u) * xtime(xtime(xtime(xtime(x))))))   \

#endif

// MixColumns function mixes the columns of the state matrix.
// The method used to multiply may be difficult to understand for the inexperienced.
// Please use the references to gain more information.
STATIC 
VOID
InvMixColumns (
  state_t* state
  )
{
  int i;
  UINT8 a, b, c, d;
  for (i = 0; i < 4; ++i)
  {
    a = (*state) [i][0];
    b = (*state) [i][1];
    c = (*state) [i][2];
    d = (*state) [i][3];

    (*state)[i][0] = (UINT8) ((UINT32) Multiply(a, 0x0eu) ^ (UINT32) Multiply(b, 0x0bu)
                     ^ (UINT32) Multiply(c, 0x0du) ^ (UINT32) Multiply(d, 0x09u));
    (*state)[i][1] = (UINT8) ((UINT32) Multiply(a, 0x09u) ^ (UINT32) Multiply(b, 0x0eu)
                     ^ (UINT32) Multiply(c, 0x0bu) ^ (UINT32) Multiply(d, 0x0du));
    (*state)[i][2] = (UINT8) ((UINT32) Multiply(a, 0x0du) ^ (UINT32) Multiply(b, 0x09u)
                     ^ (UINT32) Multiply(c, 0x0eu) ^ (UINT32) Multiply(d, 0x0bu));
    (*state)[i][3] = (UINT8) ((UINT32) Multiply(a, 0x0bu) ^ (UINT32) Multiply(b, 0x0du)
                     ^ (UINT32) Multiply(c, 0x09u) ^ (UINT32) Multiply(d, 0x0eu));
  }
}


// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
STATIC
VOID
InvSubBytes (
  state_t* state
  )
{
  UINT8 i, j;
  for (i = 0; i < 4; ++i)
  {
    for (j = 0; j < 4; ++j)
    {
      (*state)[j][i] = getSBoxInvert ((*state)[j][i]);
    }
  }
}

STATIC
VOID
InvShiftRows (
  state_t* state
  )
{
  UINT8 temp;

  // Rotate first row 1 columns to right
  temp = (*state)[3][1];
  (*state)[3][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[0][1];
  (*state)[0][1] = temp;

  // Rotate second row 2 columns to right
  temp = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;

  temp = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  // Rotate third row 3 columns to right
  temp = (*state)[0][3];
  (*state)[0][3] = (*state)[1][3];
  (*state)[1][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[3][3];
  (*state)[3][3] = temp;
}


// Cipher is the main function that encrypts the PlainText.
STATIC
VOID
Cipher (
  state_t* state,
  UINT8* RoundKey
  )
{
  UINT8 round = 0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey(0, state, RoundKey);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr-1 rounds are executed in the loop below.
  for (round = 1; round < Nr; ++round)
  {
    SubBytes (state);
    ShiftRows (state);
    MixColumns (state);
    AddRoundKey (round, state, RoundKey);
  }

  // The last round is given below.
  // The MixColumns function is not here in the last round.
  SubBytes (state);
  ShiftRows (state);
  AddRoundKey (Nr, state, RoundKey);
}

STATIC
VOID
InvCipher (
  state_t* state,
  UINT8* RoundKey
  )
{
  UINT8 round = 0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey (Nr, state, RoundKey);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr-1 rounds are executed in the loop below.
  for (round = (Nr - 1); round > 0; --round)
  {
    InvShiftRows (state);
    InvSubBytes (state);
    AddRoundKey (round, state, RoundKey);
    InvMixColumns (state);
  }

  // The last round is given below.
  // The MixColumns function is not here in the last round.
  InvShiftRows (state);
  InvSubBytes (state);
  AddRoundKey (0, state, RoundKey);
}


/*****************************************************************************/
/* Public functions:                                                         */
/*****************************************************************************/


#if defined(CBC) && (CBC == 1)


STATIC
VOID
XorWithIv (
  UINT8* buf,
  CONST UINT8* Iv
  )
{
  UINT8 i;
  for (i = 0; i < AES_BLOCKLEN; ++i) // The block in AES is always 128bit no matter the key size
  {
    buf[i] ^= Iv[i];
  }
}

VOID
AES_CBC_encrypt_buffer (
  struct AES_ctx *ctx,
  UINT8* buf,
  UINT32 length
  )
{
  uintptr_t i;
  UINT8 *Iv = ctx->Iv;
  for (i = 0; i < length; i += AES_BLOCKLEN)
  {
    XorWithIv (buf, Iv);
    Cipher ((state_t*)buf, ctx->RoundKey);
    Iv = buf;
    buf += AES_BLOCKLEN;
    //printf("Step %d - %d", i/16, i);
  }
  /* store Iv in ctx for next call */
  CopyMem (ctx->Iv, Iv, AES_BLOCKLEN);
}

VOID
AES_CBC_decrypt_buffer (
  struct AES_ctx* ctx,
  UINT8* buf,
  UINT32 length
  )
{
  uintptr_t i;
  UINT8 storeNextIv[AES_BLOCKLEN];
  for (i = 0; i < length; i += AES_BLOCKLEN)
  {
    CopyMem (storeNextIv, buf, AES_BLOCKLEN);
    InvCipher ((state_t*)buf, ctx->RoundKey);
    XorWithIv (buf, ctx->Iv);
    CopyMem (ctx->Iv, storeNextIv, AES_BLOCKLEN);
    buf += AES_BLOCKLEN;
  }

}

#endif // #if defined(CBC) && (CBC == 1)



#if defined(CTR) && (CTR == 1)

/* Symmetrical operation: same function for encrypting as for decrypting. Note any IV/nonce should never be reused with the same key */
VOID
AES_CTR_xcrypt_buffer (
  struct AES_ctx* ctx,
  UINT8* buf,
  UINT32 length
  )
{
  UINT8 buffer[AES_BLOCKLEN];

  unsigned i;
  int bi;
  for (i = 0, bi = AES_BLOCKLEN; i < length; ++i, ++bi)
  {
    if (bi == AES_BLOCKLEN) /* we need to regen xor compliment in buffer */
    {

      CopyMem (buffer, ctx->Iv, AES_BLOCKLEN);
      Cipher ((state_t*)buffer, ctx->RoundKey);

      /* Increment Iv and handle overflow */
      for (bi = (AES_BLOCKLEN - 1); bi >= 0; --bi)
      {
        /* inc will owerflow */
        if (ctx->Iv[bi] == 255)
        {
          ctx->Iv[bi] = 0;
          continue;
        }
        ctx->Iv[bi] += 1;
        break;
      }
      bi = 0;
    }

    buf[i] = (buf[i] ^ buffer[bi]);
  }
}

#endif // #if defined(CTR) && (CTR == 1)
