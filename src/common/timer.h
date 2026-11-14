// timer.h - a tiny stopwatch used by the benchmarks and the tests.
#ifndef FA_TIMER_H
#define FA_TIMER_H

#include <algorithm>
#include <chrono>
#include <vector>

namespace fa {

// Returns the current time in milliseconds (monotonic, never goes backwards).
inline double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

// Collects many timing samples and reports the median / percentile.
// The median ignores one-off slow runs, so it is better than the average.
class TimingStats {
public:
    void add(double ms) { samples_.push_back(ms); }
    size_t count() const { return samples_.size(); }
    double median() const { return percentile(50); }
    double p95() const { return percentile(95); }
    double max() const { return samples_.empty() ? 0 : *std::max_element(samples_.begin(), samples_.end()); }
    double percentile(double p) const {
        if (samples_.empty()) return 0;
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t index = static_cast<size_t>((p / 100.0) * (sorted.size() - 1) + 0.5);
        return sorted[index];
    }

private:
    std::vector<double> samples_;
};

}  // namespace fa

#endif  // FA_TIMER_H
