#include "../common/communication.h"
#include "../common/storage.h"

int main()
{
    Communication comms;
    if (comms.wait_connection(4301))
    {
        return 1;
    }

    Storage store("../../dataset/data/", 150, 1024000000, 64000);

    bool conected = true;
    while (conected)
    {
        int file_idx, block_idx;
        comms.read_int(&file_idx);
        comms.read_int(&block_idx);

        if (file_idx == -1)
        {
            comms.close_connection();
            conected = false;
            return 0;
        }

        uint32_t hash = store.block_hash(file_idx, block_idx);
        comms.send_buf(&hash, sizeof(hash));
    }
}
