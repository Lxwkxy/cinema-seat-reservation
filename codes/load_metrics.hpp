#ifndef LOAD_METRICS_HPP
#define LOAD_METRICS_HPP

#include <algorithm>
#include <cstddef>
#include <vector>

// เก็บ latency ของ Request ที่เริ่มส่งแล้ว ทั้ง Request ที่สำเร็จและล้มเหลว
struct LatencyMetrics {
    long long total_latency_us = 0;
    std::vector<long long> samples_us;

    // เพิ่มเวลา Request หนึ่งครั้งลงทั้งผลรวมและรายการตัวอย่าง
    void record(long long latency_us) {
        total_latency_us += latency_us;
        samples_us.push_back(latency_us);
    }

    // รวมตัวอย่างทั้งหมดเพื่อให้คำนวณ percentile จากข้อมูลรวมได้
    void merge(const LatencyMetrics& other) {
        total_latency_us += other.total_latency_us;
        samples_us.insert(samples_us.end(), other.samples_us.begin(),
                          other.samples_us.end());
    }

    // คืนจำนวน Request ที่มีการบันทึก latency
    long long sample_count() const {
        return static_cast<long long>(samples_us.size());
    }

    // คืนค่าเฉลี่ยเป็นมิลลิวินาที หรือ 0 เมื่อยังไม่มีตัวอย่าง
    double average_milliseconds() const {
        if (samples_us.empty()) return 0.0;
        return static_cast<double>(total_latency_us) /
               static_cast<double>(samples_us.size()) / 1000.0;
    }

    // เรียงตัวอย่างและเลือกค่า P95 ด้วยวิธี nearest-rank
    double percentile95_milliseconds() const {
        if (samples_us.empty()) return 0.0;
        std::vector<long long> sorted_samples = samples_us;
        std::sort(sorted_samples.begin(), sorted_samples.end());
        const std::size_t sample_size = sorted_samples.size();
        const std::size_t rank = (sample_size * 95 + 99) / 100;
        const std::size_t index = rank - 1;
        return static_cast<double>(sorted_samples[index]) / 1000.0;
    }

    // คืน latency สูงสุดเป็นมิลลิวินาที หรือ 0 เมื่อไม่มีตัวอย่าง
    double maximum_milliseconds() const {
        if (samples_us.empty()) return 0.0;
        const auto maximum =
            *std::max_element(samples_us.begin(), samples_us.end());
        return static_cast<double>(maximum) / 1000.0;
    }
};

#endif
