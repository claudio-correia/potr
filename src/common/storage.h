#pragma once

#include <string>
#include <vector>

class Storage
{
private:
    std::string data_dir;

    std::string format_file_path(int file_idx);

public:
    int file_size;
    int block_size;
    int file_count;

    Storage(std::string _data_dir, int _file_count, int _file_size, int _block_size)
    {
        this->data_dir = _data_dir;
        this->file_count = _file_count;
        this->file_size = _file_size;
        this->block_size = _block_size;
    }

    uint32_t block_hash();

    uint32_t block_hash(int file_idx, int block_idx);
};