#include "results_tracker.h"

#include <iomanip>
#include <sstream>

void ResultsTracker::add(std::string experiment_name, long x)
{
    this->results.emplace(experiment_name, std::vector<long>());
    this->results.at(experiment_name).push_back(x);
}

void ResultsTracker::save_experiments()
{
    auto t = std::time(nullptr);
    auto tm = *std::gmtime(&t);

    for (auto &it : this->results)
    {
        std::ostringstream out;
        out << this->results_dir << "/" << std::put_time(&tm, "%Y-%m-%d_%H-%M") << "_" << it.first << ".txt";

        FILE *f = fopen(out.str().c_str(), "w");
        for (auto &r : it.second)
        {
            fprintf(f, "%ld\n", r);
        }
    }

    this->results.clear();
}