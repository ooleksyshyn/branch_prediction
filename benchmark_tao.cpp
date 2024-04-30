#include "benchmark_tao.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "/tao/json.hpp"

namespace {
    std::string readStringFromFile(const std::string& file_path) {
        const std::ifstream input_stream(file_path, std::ios_base::binary);

        if (input_stream.fail()) {
            throw std::runtime_error("Failed to open file " + file_path);
        }

        std::stringstream buffer;
        buffer << input_stream.rdbuf();

        return buffer.str();
    }
}

void benchmartTaoJson()
{
    std::array examples{
        "../tao/example1.json",
        "../tao/example2.json",
        "../tao/example3.json",
        "../tao/example4.json"
    };
    const auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < 10; ++i) {
        for (const auto& example : examples) {
            auto stringJson = readStringFromFile(example);
            auto json = tao::json::from_string(stringJson);
        }
    }

    const std::chrono::duration<double> diff =
            std::chrono::high_resolution_clock::now() - start;

    std::cout << "Time: " << std::fixed << std::setprecision(6) << diff.count()
                << " sec (no attributes)" << std::endl; 
}