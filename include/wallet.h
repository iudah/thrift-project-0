#ifndef _WALLET_H_
#define _WALLET_H_

#include "book.h"

#define THRIFT_KEY_LEN (65)
// #define THRIFT_KEY_LEN crypto_box_SEEDBYTES
keystore *create_wallet(char *password);
bool keystore_exists();

#endif