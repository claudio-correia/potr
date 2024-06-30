#include "../common/communication.h"
#include "../common/results_tracker.h"

#include <iostream>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstring>
#include <functional>
#include <chrono>
#include <cmath>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ios>
#include <math.h>    /* log10 */
#include <algorithm> // std::min
#include <list>
#include <stdlib.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

#define EXPERIMENT 10

using namespace std;

static const unsigned char gcm_keyE[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

#define SGX_AESGCM_MAC_SIZE 16
#define SGX_AESGCM_IV_SIZE 12
int nonce_size = 8;

ResultsTracker results("../../results");
std::string current_experiment = "";

const char *localhost = "127.0.0.1"; // ssh reverse proxy

class Auditor
{
public:
    Communication comms;
    Communication comms_reader_near;
    Communication comms_reader_sgx;
    Communication comms_reader_tagus;
    Communication comms_reader_netherlands;

    Auditor()
    {
        comms.connect_to(localhost, 4300);
        comms_reader_near.connect_to(localhost, 4305);
        comms_reader_sgx.connect_to(localhost, 4304);
        comms_reader_tagus.connect_to(localhost, 4303);
        comms_reader_netherlands.connect_to(localhost, 4302);
    }

    void aes_gcm_encrypt(unsigned char *decMessageIn, size_t len, unsigned char *encMessageOut, int lenOut)
    {

        EVP_CIPHER_CTX *ctx;
        int outlen;

        ctx = EVP_CIPHER_CTX_new();
        /* Set cipher type and mode */
        EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL);
        /* Set IV length if default 96 bits is not appropriate */
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, SGX_AESGCM_IV_SIZE, NULL);
        // generate IV
        RAND_bytes((unsigned char *)encMessageOut + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE); //
        /* Initialise key and IV */
        EVP_EncryptInit_ex(ctx, NULL, NULL, gcm_keyE, (unsigned char *)encMessageOut + SGX_AESGCM_MAC_SIZE);
        EVP_EncryptUpdate(ctx, (unsigned char *)encMessageOut + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE, &outlen, (uint8_t *)decMessageIn, len);
        /* Output encrypted block */
        EVP_EncryptFinal_ex(ctx, (unsigned char *)(encMessageOut + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE), &outlen);
        /* Get MAC */
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, SGX_AESGCM_MAC_SIZE, encMessageOut);
        EVP_CIPHER_CTX_free(ctx);
    }

    void getNonce(int nonce_size, unsigned char *nonce)
    {
        int r = RAND_bytes(nonce, nonce_size);
        if (r != 1)
        {
            perror("getNonce: ");
            exit(EXIT_FAILURE);
        }
    }

    void runChallenge(int N_value, int reps, int remoteSource, int localRatio)
    {
        cout << " running challenge : N" << N_value << endl;

        unsigned char files_nonce[nonce_size];
        size_t encMessageLen = (SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE + nonce_size);

        for (int i = 0; i < reps; i++)
        {
            usleep(200);

            getNonce(nonce_size, files_nonce);
            unsigned char *encMessage = (unsigned char *)malloc((encMessageLen + 1) * sizeof(char));
            aes_gcm_encrypt(files_nonce, nonce_size, encMessage, encMessageLen);

            comms.send_int(1);
            comms.send_int(N_value);
            comms.send_int(remoteSource);
            comms.send_int(localRatio);
            comms.send_int(current_experiment.length());
            comms.send_int(encMessageLen);
            comms.send_buf(current_experiment.c_str(), current_experiment.length() + 1);

            const auto startTimer = std::chrono::high_resolution_clock::now();

            comms.send_buf(encMessage, encMessageLen);

            unsigned char solution[32];
            comms.read_buf(solution, 32);

            const auto endTimer = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTimer - startTimer);
            results.add(current_experiment + "_delay", duration.count());

            cout << " nrepeats: " << i << " time: " << duration.count() * 0.000001 << "\n";
        }

        // check solution, TODO

        // printHexOcall(" solution : ", solution, 32);
        // cout << " challenge duration: " << (end-start)*0.000001  << " seconds " << endl;
    }

    void runChallenge(int N_value, int reps, int remoteSource)
    {
        runChallenge(N_value, reps, remoteSource, 0);
    }

    void runChallenge(int N_value, int reps)
    {
        runChallenge(N_value, reps, 0, 0);
    }

    void measureRTT(int reps)
    {
        for (int i = 0; i < reps; i++)
        {
            usleep(200);

            const auto startTimer = std::chrono::high_resolution_clock::now();

            comms.send_int(2);
            int result;
            comms.read_int(&result);

            const auto endTimer = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTimer - startTimer);
            results.add(current_experiment + "_rtt", duration.count());
        }
    }

    void saveResults()
    {
        results.save_experiments();
        comms.send_int(3);
    }

    void readerLoad(const std::string &current_experiment, int readers_0, int readers_1, int readers_2, int readers_3, int sleep_us)
    {
        const std::string near_experiment = current_experiment + "_near";
        comms_reader_near.send_int(readers_0);
        comms_reader_near.send_int(sleep_us);
        comms_reader_near.send_int(near_experiment.length());
        comms_reader_near.send_buf(near_experiment.c_str(), near_experiment.length() + 1);

        const std::string sgx_experiment = current_experiment + "_sgx";
        comms_reader_sgx.send_int(readers_1);
        comms_reader_sgx.send_int(sleep_us);
        comms_reader_sgx.send_int(sgx_experiment.length());
        comms_reader_sgx.send_buf(sgx_experiment.c_str(), sgx_experiment.length() + 1);

        const std::string tagus_experiment = current_experiment + "_tagus";
        comms_reader_tagus.send_int(readers_2);
        comms_reader_tagus.send_int(sleep_us);
        comms_reader_tagus.send_int(tagus_experiment.length());
        comms_reader_tagus.send_buf(tagus_experiment.c_str(), tagus_experiment.length() + 1);

        const std::string netherlands_experiment = current_experiment + "_netherlands";
        comms_reader_netherlands.send_int(readers_3);
        comms_reader_netherlands.send_int(sleep_us);
        comms_reader_netherlands.send_int(netherlands_experiment.length());
        comms_reader_netherlands.send_buf(netherlands_experiment.c_str(), netherlands_experiment.length() + 1);
    }

    void readerLoad(const std::string &current_experiment, int readers, int sleep_us)
    {
        readerLoad(current_experiment, readers, readers, readers, readers, sleep_us);
    }

    void close()
    {
        comms.send_int(-1);
        comms.close_connection();
        comms_reader_sgx.send_int(-1);
        comms_reader_sgx.send_int(-1);
        comms_reader_sgx.close_connection();
        comms_reader_near.send_int(-1);
        comms_reader_near.send_int(-1);
        comms_reader_near.close_connection();
        comms_reader_tagus.send_int(-1);
        comms_reader_tagus.send_int(-1);
        comms_reader_tagus.close_connection();
        comms_reader_netherlands.send_int(-1);
        comms_reader_netherlands.send_int(-1);
        comms_reader_netherlands.close_connection();
    }
};

int main()
{
    cout << "Hello from auditor" << endl;

    Auditor auditor = Auditor();

#if EXPERIMENT == 0
    current_experiment = "replicate_honest";

    int N = 250;
    int reps = 200;

    auditor.measureRTT(reps);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.runChallenge(N, reps);

    auditor.saveResults();
#elif EXPERIMENT == 1
    current_experiment = "sweep_usage";

    int readers = 0;
    int sleep_us = 100;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 2;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 4;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 6;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 8;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 10;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 12;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 14;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 16;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 18;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 20;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 22;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 24;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 26;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 28;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 30;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 32;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 34;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 36;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 38;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);

    readers = 40;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    auditor.readerLoad(current_experiment, readers, sleep_us);

    sleep(10);
#elif EXPERIMENT == 2
    current_experiment = "usage_impact";

    int N = 250;
    int reps = 1000;
    int sleep_us = 100;
    int readers = 14;

    auditor.measureRTT(reps);
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 16;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 18;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 20;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 22;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 24;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 26;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 28;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);

    readers = 30;
    results.add(current_experiment + "_readers", readers);
    results.add(current_experiment + "_sleep", readers);
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    auditor.readerLoad(current_experiment, readers, sleep_us);
    auditor.runChallenge(N, reps);
#elif EXPERIMENT == 3
    current_experiment = "remote_potr";

    int N = 250;
    int reps = 100;

    auditor.measureRTT(reps);
    auditor.readerLoad(current_experiment, 11, 22, 11, 6, 100);

    int remoteSource = 0; // local
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 1; // near
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 2; // tagus
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 3; // netherlands
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);
#elif EXPERIMENT == 4
    current_experiment = "remote_potr_no_load";

    int N = 250;
    int reps = 100;

    auditor.measureRTT(reps);

    int remoteSource = 0; // local
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 1; // near
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 2; // tagus
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 3; // netherlands
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);
#elif EXPERIMENT == 5
    current_experiment = "mixed";

    int N = 250;
    int reps = 200;

    auditor.measureRTT(reps);
    auditor.readerLoad(current_experiment, 11, 22, 11, 6, 100);

    int remoteSource = 0; // local
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 2; // tagus
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 3; // netherlands 10%
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", 3);
    auditor.runChallenge(N, reps, remoteSource, 90);

    // netherlands 25%
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", 4);
    auditor.runChallenge(N, reps, remoteSource, 75);
#elif EXPERIMENT == 6
    current_experiment = "mixed_multiple_n";

    int reps = 200;

    auditor.measureRTT(reps);
    auditor.readerLoad(current_experiment, 11, 22, 11, 6, 100);

    int remote = 0;
    int N = 5;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 20;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 250;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 5;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 10;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 20;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 50;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 100;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 250;
    remote = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 25;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);
#elif EXPERIMENT == 7
    current_experiment = "poutr_accuracy_v2";

    int reps_poatr = 100;
    int reps = 4000;
    int remote = 10;

    auditor.measureRTT(reps_poatr);
    auditor.readerLoad(current_experiment, 11, 22, 11, 6, 100);

    int N = 250;
    results.add(current_experiment + "_reps", reps_poatr);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps_poatr, 3, 100 - remote);

    N = 40;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 20;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 0;
    N = 250;
    results.add(current_experiment + "_reps", reps_poatr);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps_poatr);

    N = 40;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 20;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);
#elif EXPERIMENT == 8
    current_experiment = "poutr_accuracy_v2_no_load";

    int reps_poatr = 100;
    int reps = 4000;
    int remote = 5;

    auditor.measureRTT(reps_poatr);

    int N = 250;
    results.add(current_experiment + "_reps", reps_poatr);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps_poatr, 3, 100 - remote);

    N = 40;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 20;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    N = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps, 3, 100 - remote);

    remote = 0;
    N = 250;
    results.add(current_experiment + "_reps", reps_poatr);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps_poatr);

    N = 40;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 20;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);

    N = 10;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote", remote);
    auditor.runChallenge(N, reps);
#elif EXPERIMENT == 9
    current_experiment = "sweep_n";

    int reps = 100;
    int N = 10;
    int remoteSource = 1;

    auditor.measureRTT(reps);
    auditor.readerLoad(current_experiment, 11, 22, 11, 6, 100);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 85;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 250;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 500;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 0;
    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 85;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 250;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 500;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);
#elif EXPERIMENT == 10
    current_experiment = "sweep_n_no_load";

    int reps = 100;
    int N = 10;
    int remoteSource = 1;

    auditor.measureRTT(reps);

    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 85;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 250;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 500;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    remoteSource = 0;
    N = 30;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 50;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 85;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 100;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 250;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);

    N = 500;
    results.add(current_experiment + "_reps", reps);
    results.add(current_experiment + "_n", N);
    results.add(current_experiment + "_remote_source", remoteSource);
    auditor.runChallenge(N, reps, remoteSource);
#endif

    results.save_experiments();

    auditor.close();
}
