#include "storage.h"

#include <iomanip>
#include <math.h>
#include <sstream>
#include <openssl/sha.h>

std::string Storage::format_file_path(int file_idx)
{
    int padding = (this->file_count == 0) ? 1 : (log10(this->file_count) + 1);

    std::ostringstream out;
    out << this->data_dir << "/file";
    out << std::internal << std::setfill('0') << std::setw(padding) << file_idx;
    out << ".dat";
    return out.str();
}

uint32_t Storage::block_hash()
{
    int f = random() % this->file_count;
    int b = random() % (this->file_size / this->block_size);
    return block_hash(f, b);
}

uint32_t Storage::block_hash(int file_idx, int block_idx)
{
    char memblock[this->block_size];
    FILE *f = fopen(format_file_path(file_idx).c_str(), "rb");
    if (!f)
    {
        perror("Failed to open block's file");
    }
    fseek(f, block_idx * this->block_size, SEEK_SET);
    fread(memblock, this->block_size, 1, f);
    fclose(f);

    SHA256_CTX c;
    SHA256_Init(&c);
    SHA256_Update(&c, memblock, block_size);

    unsigned char hash[32];
    SHA256_Final(hash, &c);
    return *reinterpret_cast<uint32_t *>(hash);
}
