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

#include <communication.h>
#include <storage.h>
#include <results_tracker.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "sgx_error.h" /* sgx_status_t */
#include "sgx_eid.h"   /* sgx_enclave_id_t */
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <stdlib.h> /* srand, rand */
#include <iomanip>
#include <sstream>
#include <ios>
#include <math.h>    /* log10 */
#include <algorithm> // std::min
#include <list>
#include <openssl/sha.h>
#include <pwd.h>
#include <sgx_urts.h>
#include <sgx_uswitchless.h>
#include <random>
#include "Enclave_u.h"

static const unsigned char gcm_keyE[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

#define SGX_AESGCM_MAC_SIZE 16
#define SGX_AESGCM_IV_SIZE 12

#define TOKEN_FILENAME "enclave.token"
#define ENCLAVE_FILENAME "enclave.signed.so"

sgx_enclave_id_t global_eid = 0;

#define MAX_PATH FILENAME_MAX

int nonce_size = 8;
int last_remote_source = 0;

bool running = true;

const char *remote_storage_ip[3] = {
    "146.193.41.141", // near
    "127.0.0.1",      // tagus - ssh reverse proxy
    "51.136.20.214"   // netherlands
};

Storage store("../../dataset/data", 150, 1024000000, 64000);
ResultsTracker results("../../results");
std::string current_experiment = "";

void ocall_empty(void) {}
void ocall_empty_switchless(void) {}

std::string char_to_hex_ocall(const unsigned char *hash, int size)
{

    std::stringstream ss;
    for (int i = 0; i < size; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    std::string digest = ss.str();
    std::transform(digest.begin(), digest.end(), digest.begin(), ::toupper);

    return digest;
}

void printHexOcall(const char *str, const unsigned char *hash, size_t size)
{
    std::cout << str << char_to_hex_ocall(hash, size) << std::endl;
}

void print_char_ocall(unsigned char *text, int TextLen)
{
    std::cout << "Ocall: " << text << std::endl;
}

int initialize_enclave(const sgx_uswitchless_config_t *us_config)
{
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;

    const void *enclave_ex_p[32] = {0};

    enclave_ex_p[SGX_CREATE_ENCLAVE_EX_SWITCHLESS_BIT_IDX] = (const void *)us_config;

    ret = sgx_create_enclave_ex(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL, SGX_CREATE_ENCLAVE_EX_SWITCHLESS, enclave_ex_p);
    if (ret != SGX_SUCCESS)
    {
        std::cerr << "Error initializing enclave [" << ret << "]" << std::endl;
        return -1;
    }

    return 0;
}

void run_challenge(Communication *comms_remote_storage, int localRatio, int N, unsigned char *encMessage, int encMessageLen, unsigned char *result)
{
    int file_index;
    int block_index;
    ecall_challenge_first_step(global_eid, &file_index, &block_index, encMessage, encMessageLen, nonce_size, store.file_size, store.block_size, store.file_count);

    for (int i = 0; i < N; i++)
    {
        const auto t0 = std::chrono::high_resolution_clock::now();
        uint32_t hash;
        if (comms_remote_storage && (rand() % 101) > localRatio)
        {
            comms_remote_storage->send_int(file_index);
            comms_remote_storage->send_int(block_index);
            comms_remote_storage->read_buf(&hash, sizeof(hash));
        }
        else
        {
            hash = store.block_hash(file_index, block_index);
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        ecall_challenge_next_step_switchless(global_eid, &file_index, &block_index, reinterpret_cast<unsigned char *>(&hash), SHA256_DIGEST_LENGTH, store.file_size, store.block_size, store.file_count);
        const auto t2 = std::chrono::high_resolution_clock::now();

        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0);
        const auto enclave_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1);

        results.add(current_experiment + "_real_delay", duration.count());
        results.add(current_experiment + "_real_delay_enclave", enclave_duration.count());
    }

    ecall_challenge_final_step(global_eid, result);
}

void server_handler()
{
    Communication comms;
    if (comms.wait_connection(4300))
    {
        return;
    }

    bool connected = true;
    while (connected)
    {
        int request_type;
        comms.read_int(&request_type);
        switch (request_type)
        {
        case 1: /* RUN CHALLENGE (N, remoteSource, remoteRatio, experimentNameLen, encMessageLen, experimentName, encMessage) -> (solution) */
        {
            int N, remoteSource, localRatio, experimentNameLen, encMessageLen;
            comms.read_int(&N);
            comms.read_int(&remoteSource);
            comms.read_int(&localRatio);
            comms.read_int(&experimentNameLen);
            comms.read_int(&encMessageLen);

            Communication comms_remote_storage;
            if (remoteSource > 0 && last_remote_source != remoteSource)
            {
                if (last_remote_source > 0)
                {
                    comms_remote_storage.send_int(-1);
                    comms_remote_storage.close_connection();
                }

                comms_remote_storage.connect_to(remote_storage_ip[remoteSource - 1], 4301);
                last_remote_source = remoteSource;
            }

            char *experimentName = (char *)malloc(experimentNameLen + 1);
            comms.read_buf(experimentName, experimentNameLen + 1);
            current_experiment = std::string(experimentName);

            unsigned char *encMessage = (unsigned char *)malloc(encMessageLen + 1);
            comms.read_buf(encMessage, encMessageLen);

            unsigned char solution[32];
            run_challenge(remoteSource > 0 ? &comms_remote_storage : nullptr, localRatio, N, encMessage, encMessageLen, solution);
            comms.send_buf(solution, 32);

            break;
        }
        case 2: /* RTT () -> (200) */
            comms.send_int(200);
            break;
        case 3: /* SAVE () -> () */
            results.save_experiments();
            break;
        default:
            results.save_experiments();

            comms.close_connection();
            connected = false;
        }
    }
}

/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    srand(NULL);

    /* Configuration for Switchless SGX */
    sgx_uswitchless_config_t us_config = SGX_USWITCHLESS_CONFIG_INITIALIZER;
    us_config.num_uworkers = 2;
    us_config.num_tworkers = 2;

    /* Initialize the enclave */
    if (initialize_enclave(&us_config) < 0)
    {
        printf("Error: enclave initialization failed\n");
        return -1;
    }

    server_handler();

    sgx_destroy_enclave(global_eid);
    return 0;
}
