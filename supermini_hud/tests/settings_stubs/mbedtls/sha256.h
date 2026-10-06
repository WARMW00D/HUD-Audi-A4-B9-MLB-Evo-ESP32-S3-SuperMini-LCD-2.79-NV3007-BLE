#pragma once
// Host-only adapter: exercise the firmware SHA calls against real OpenSSL SHA-256.
#include <openssl/sha.h>
#include <cstring>
typedef SHA256_CTX mbedtls_sha256_context;
inline void mbedtls_sha256_init(mbedtls_sha256_context *c){std::memset(c,0,sizeof(*c));}
inline void mbedtls_sha256_free(mbedtls_sha256_context *c){std::memset(c,0,sizeof(*c));}
inline int mbedtls_sha256_starts(mbedtls_sha256_context *c,int){return SHA256_Init(c)==1?0:-1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){return SHA256_Update(c,p,n)==1?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *p){return SHA256_Final(p,c)==1?0:-1;}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c,int n){return mbedtls_sha256_starts(c,n);}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *c,const unsigned char *p,size_t n){return mbedtls_sha256_update(c,p,n);}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c,unsigned char *p){return mbedtls_sha256_finish(c,p);}
