#pragma once
// Small deterministic RNG (splitmix64). Seedable so hands can be replayed / tested.
#include <cstdint>
#include <utility>

namespace okey {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }
    void reseed(uint64_t seed) { s_ = seed ? seed : 0x9E3779B97F4A7C15ull; }

    uint64_t next() {
        uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // [0, n)
    int range(int n) { return n <= 0 ? 0 : (int)(next() % (uint64_t)n); }
    // [lo, hi] inclusive
    int range(int lo, int hi) { return lo + range(hi - lo + 1); }
    // [0, 1)
    float uniform() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    bool chance(float p) { return uniform() < p; }

    template <class Vec>
    void shuffle(Vec& v) {
        for (int i = (int)v.size() - 1; i > 0; --i) {
            int j = range(i + 1);
            std::swap(v[i], v[j]);
        }
    }

private:
    uint64_t s_;
};

} // namespace okey
