#ifndef _DIGCALC_H_
#define _DIGCALC_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "settings.h"

#define HASHLEN 16
typedef unsigned char HASH[HASHLEN];
#define HASHHEXLEN 32
typedef char HASHHEX[HASHHEXLEN+1];
#ifdef USE_OPENSSL
#define IN const
#else
#define IN
#endif
#define OUT

LI_EXPORT void CvtHex(
    IN HASH Bin,
    OUT HASHHEX Hex
    );

#endif
