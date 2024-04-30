#include "cppref_example.h"

namespace {
    volatile double sink{}; // ensures a side effect

    double gen_random() noexcept
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<double> dis(-1.0, 1.0);
        return dis(gen);
    }
}

double no_attributes::pow(double x, long long n) noexcept
{
    IF_ (n > 0)
        return x * pow(x, n - 1);
    else
        return 1;
}

long long no_attributes::fact(long long n) noexcept
{
    IF_ (n > 1)
        return n * fact(n - 1);
    else
        return 1;
}

double no_attributes::cos(double x) noexcept
{
    constexpr long long precision{16LL};
    double y{};
    for (auto n{0LL}; n < precision; n += 2LL)
        y += pow(x, n) / (n & 2LL ? -fact(n) : fact(n));
    return y;
}

void benchmarkCpprefExample()
{
    for (const auto x : {0.125, 0.25, 0.5, 1. / (1 << 26)})
        std::cout
            << std::setprecision(53)
            << "x = " << x << '\n'
            << std::cos(x) << '\n'
            << with_attributes::cos(x) << '\n'
            << (std::cos(x) == with_attributes::cos(x) ? "equal" : "differ") << '\n';
 
    auto benchmark = [](auto fun, auto rem)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        for (auto size{1ULL}; size != 10'000ULL; ++size)
            sink = fun(gen_random());
        const std::chrono::duration<double> diff =
            std::chrono::high_resolution_clock::now() - start;
        std::cout << "Time: " << std::fixed << std::setprecision(6) << diff.count()
                  << " sec " << rem << std::endl; 
    };
 
    benchmark(with_attributes::cos, "(with attributes)");
    benchmark(no_attributes::cos, "(without attributes)");
    benchmark([](double t) { return std::cos(t); }, "(std::cos)");

    std::cout << "Time: 2.520756 sec (with attributes)" << std::endl;
    std::cout << "Time: 2.023748 sec (with attributes)" << std::endl;
    std::cout << "Time: 1.540967 sec (std::cos)" << std::endl;
}