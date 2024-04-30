#pragma once

#include <string>
#include <unordered_map>
#include <source_location>

class IfCounter
{
public:
    static IfCounter& instance();

    bool addCount(const std::string& key, bool satisfied);

    void printStats() const;

private:
    IfCounter() = default;

    struct Counter
    {
        size_t total = 0;
        size_t satisfied = 0;
    };

    std::unordered_map<std::string, Counter> counterMap;
};

#define LINE_ID(name) std::string{std::source_location::current().file_name()} + ":" + std::to_string(std::source_location::current().line()) + ":" + std::source_location::current().function_name() + ":" + name

#define IF_(condition) if (IfCounter::instance().addCount(LINE_ID("if"), condition))

#define ELSE_IF(condition) else if (IfCounter::instance().addCount(LINE_ID("else if"), condition))