#include "../common/communication.h"
#include "../common/storage.h"
#include "../common/results_tracker.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

sig_atomic_t stop = 0;
void sig_handler(int signal)
{
    stop = 1;
}

int main()
{
    std::srand(std::time(nullptr));

    Communication comms;
    if (comms.wait_connection(4302))
    {
        return 1;
    }

    ResultsTracker results("../../results");
    std::string current_experiment = "";

    Storage store("../../dataset/data/", 150, 1024000000, 64000);

    int fd[2];
    pipe(fd);

    std::vector<pid_t> readers;

    long timer_start = 0, timer_end = 0;
    bool conected = true;
    bool first = true;
    while (conected)
    {
        int readers_count, sleep_us, experimentNameLen;
        comms.read_int(&readers_count);
        comms.read_int(&sleep_us);

        timer_end = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        unsigned int total_blocks = 0;
        if (!first)
        {
            for (auto &pid : readers)
            {
                kill(pid, SIGTERM);

                unsigned int read_blocks;
                read(fd[0], &read_blocks, sizeof(unsigned int));
                total_blocks += read_blocks;
            }
            readers.clear();

            std::cout << "Read " << total_blocks / (timer_end - timer_start) << " blocks/second" << std::endl;
            results.add(current_experiment + "_throughput", total_blocks / (timer_end - timer_start));
        }
        else
        {
            first = false;
        }

        if (readers_count == -1)
        {
            results.save_experiments();
            comms.close_connection();
            conected = false;
            return 0;
        }

        comms.read_int(&experimentNameLen);

        char *experimentName = (char *)malloc(experimentNameLen + 1);
        comms.read_buf(experimentName, experimentNameLen + 1);
        current_experiment = std::string(experimentName);

        std::cout << "Forking " << readers_count << " reader proccesses" << std::endl;

        for (int i = 0; i < readers_count; i++)
        {
            auto pid = fork();
            if (pid == 0)
            {
                signal(SIGTERM, sig_handler);
                close(fd[0]);
                unsigned int read_blocks = 0;

                usleep(rand() % sleep_us);

                while (true)
                {
                    if (stop)
                    {
                        write(fd[1], &read_blocks, sizeof(unsigned int));
                        exit(0);
                    }

                    store.block_hash();
                    read_blocks++;

                    usleep(sleep_us);
                }
            }
            else if (pid > 0)
            {
                timer_start = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                readers.push_back(pid);
            }
            else
            {
                perror("Failed to fork");
                for (auto &pid : readers)
                {
                    kill(pid, SIGTERM);
                }
                comms.close_connection();
                conected = false;
                return 1;
            }
        }
    }
}
