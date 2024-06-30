/*
 * Copyright (C) 2011-2021 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "Enclave_t.h"
#include "sgx_tcrypto.h"
#include <string.h>

static sgx_aes_gcm_128bit_key_t key = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

unsigned char currentNonce[] = "6b86b273ff34fce19d6b804ef5a3f57";
// unsigned char currentDigest[SGX_SHA256_HASH_SIZE];
unsigned char currentDigest[] = "6b86b273ff34fce19d6b804ef5a3f57";

void ecall_repeat_ocalls(unsigned long nrepeats, int use_switchless)
{
    sgx_status_t (*ocall_fn)(void) = use_switchless ? ocall_empty_switchless : ocall_empty;
    while (nrepeats--)
    {
        ocall_fn();
    }
}

void ecall_empty(void) {}
void ecall_empty_switchless(void) {}

void decryptMessage(char *encMessageIn, size_t len, uint8_t *decMessageOut, size_t lenOut)
{
    uint8_t *encMessage = (uint8_t *)encMessageIn;

    sgx_rijndael128GCM_decrypt(
        &key,
        encMessage + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
        lenOut,
        decMessageOut,
        encMessage + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
        NULL, 0,
        (sgx_aes_gcm_128bit_tag_t *)encMessage);
}

void checksumSHA256(unsigned char *text1, int size1, unsigned char *text2, int size2, unsigned char *result)
{

    sgx_sha_state_handle_t context = NULL;
    sgx_sha256_init(&context);
    sgx_sha256_update(text1, size1, context);
    if (text2 != nullptr && size2 != 0)
        sgx_sha256_update(text2, size2, context);
    sgx_sha256_get_hash(context, (sgx_sha256_hash_t *)result);
    sgx_sha256_close(context);
}

uint32_t getULong(unsigned char *hash)
{
    return *reinterpret_cast<uint32_t *>(hash);
}

void ecall_challenge_next_step(int *fileIndex, int *blockIndex, unsigned char *lastDigest, int lastDigestLen, int fileSize, int blockSize, int fileCount)
{

    unsigned char nextFile[SGX_SHA256_HASH_SIZE];

    checksumSHA256(lastDigest, lastDigestLen, currentDigest, 32, nextFile);

    uint32_t next_value = getULong(nextFile);

    memcpy(currentDigest, nextFile, SGX_SHA256_HASH_SIZE);

    *fileIndex = (next_value & 0xFF) % fileCount;
    *blockIndex = (next_value >> 16) % (fileSize / blockSize);
}

void ecall_challenge_next_step_switchless(int *fileIndex, int *blockIndex, unsigned char *lastDigest, int lastDigestLen, int fileSize, int blockSize, int fileCount)
{

    unsigned char nextFile[SGX_SHA256_HASH_SIZE];

    checksumSHA256(lastDigest, lastDigestLen, currentDigest, 32, nextFile);

    uint32_t next_value = getULong(nextFile);

    memcpy(currentDigest, nextFile, SGX_SHA256_HASH_SIZE);

    *fileIndex = (next_value & 0xFF) % fileCount;
    *blockIndex = (next_value >> 16) % (fileSize / blockSize);
}

void ecall_challenge_first_step(int *fileIndex, int *blockIndex, unsigned char *enc_files_nonce, int encMessageLen, int nonce_size, int fileSize, int blockSize, int fileCount)
{

    size_t decMessageLen = encMessageLen - SGX_AESGCM_MAC_SIZE - SGX_AESGCM_IV_SIZE;
    unsigned char files_nonce[decMessageLen];

    decryptMessage((char *)enc_files_nonce, encMessageLen, (uint8_t *)files_nonce, decMessageLen);

    // printHexOcall("nonce: " , files_nonce, nonce_size);

    checksumSHA256(files_nonce, decMessageLen, currentNonce, 32, currentDigest); // bastava passar uma vez o nonce

    uint32_t next_value = getULong(currentDigest);

    *fileIndex = (next_value & 0xFF) % fileCount;
    *blockIndex = (next_value >> 16) % (fileSize / blockSize);
}

void ecall_challenge_final_step(unsigned char *final_hash)
{
    memcpy(final_hash, currentDigest, SGX_SHA256_HASH_SIZE);
}
