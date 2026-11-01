#pragma once

// Kosmische: deterministisk slumpgenerator (PCG32, O'Neill).
// Egen implementation och egna fordelningar i stallet for std::*_distribution,
// sa att samma seed ger exakt samma noter oavsett kompilator och version.

#include <cstdint>
#include <vector>

namespace kos
{

class Random
{
public:
    explicit Random (std::uint64_t seed = 1) { reseed (seed); }

    void reseed (std::uint64_t seed)
    {
        state = 0;
        inc = (seed << 1u) | 1u;
        next();
        state += seed ^ 0x853c49e6748fea9bULL;
        next();
    }

    std::uint32_t next()
    {
        const std::uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const auto xorshifted = (std::uint32_t) (((old >> 18u) ^ old) >> 27u);
        const auto rot = (std::uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, 1)
    double uniform() { return next() * (1.0 / 4294967296.0); }

    // [lo, hi]
    double uniform (double lo, double hi) { return lo + (hi - lo) * uniform(); }

    // Heltal i [lo, hi] (bada inklusive), utan snedfordelning
    int range (int lo, int hi)
    {
        if (hi <= lo)
            return lo;

        const auto span = (std::uint32_t) ((std::int64_t) hi - (std::int64_t) lo + 1);
        const auto threshold = (std::uint32_t) ((0x100000000ULL - span) % span);

        for (;;)
        {
            const auto r = next();
            if (r >= threshold)
                return lo + (int) (r % span);
        }
    }

    bool chance (double p)
    {
        if (p <= 0.0) return false;
        if (p >= 1.0) return true;
        return uniform() < p;
    }

    template <typename T>
    const T& pick (const std::vector<T>& items)
    {
        return items[(size_t) range (0, (int) items.size() - 1)];
    }

    // Index enligt vikter (alla >= 0, summa > 0)
    int weighted (const std::vector<double>& weights)
    {
        double sum = 0.0;
        for (auto w : weights)
            sum += w;

        double r = uniform() * sum;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (r < weights[i])
                return (int) i;
            r -= weights[i];
        }
        return (int) weights.size() - 1;
    }

private:
    std::uint64_t state = 0, inc = 1;
};

// Harleder ett nytt seed ur ett annat (SplitMix64). Anvands for att ge varje
// aspekt (monster, mutation, velocity ...) en egen slumpstrom, sa att en andrad
// parameter for t.ex. mutation inte andrar grundmonstret.
inline std::uint32_t deriveSeed (std::uint32_t seed, std::uint32_t salt)
{
    std::uint64_t z = ((std::uint64_t) seed << 32) ^ (salt * 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= z >> 31;
    return (std::uint32_t) (z ^ (z >> 32));
}

} // namespace kos
