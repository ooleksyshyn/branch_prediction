#include "if_counter.h"

#include <iostream>
#include <vector>
#include <utility>
#include <string_view>
#include <algorithm>

IfCounter& IfCounter::instance()
{
    static IfCounter inst;
    return inst;
}

bool IfCounter::addCount(const std::string& key, bool satisfied)
{
    auto& counter = counterMap[key];
    ++counter.total;
    counter.satisfied += satisfied;
    return satisfied;
}

void IfCounter::printStats() const
{
    auto result = std::vector<std::pair<std::string_view, double>>{};
    result.reserve(counterMap.size());
    
    for (const auto& [key, stats] : counterMap) {
        result.emplace_back(key, stats.satisfied / static_cast<double>(stats.total));
    }

    std::sort(result.begin(), result.end(), [] (const auto& l, const auto& r) {
        return l.second < r.second;
    });

    std::cout << "Frequencies:"<< std::endl;

    size_t i = 0;

    for (const auto& [key, frequency] : result) {
        ++i;
        // std::cout << key << ": " << frequency << std::endl;
        std::cout << i << " " << frequency << std::endl;
    }

    std::cout << "Total records: " << result.size() << std::endl;

}