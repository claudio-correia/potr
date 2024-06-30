#pragma once

#include <string>
#include <vector>
#include <unordered_map>

class ResultsTracker
{
private:
    std::string results_dir;
    std::unordered_map<std::string, std::vector<long>> results{};

public:
    ResultsTracker(std::string _results_dir)
    {
        this->results_dir = _results_dir;
    }

    void add(std::string experiment_name, long x);
    void save_experiments();
};