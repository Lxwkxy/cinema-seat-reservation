#include "load_metrics.hpp"

#include <cmath>
#include <iostream>

namespace {

// เปรียบเทียบค่า floating point โดยยอมรับความคลาดเคลื่อนเล็กน้อย
bool nearly_equal(double left, double right) {
    return std::fabs(left - right) < 0.000001;
}

// รายงานชื่อกรณีที่ไม่ผ่านและคืนสถานะให้ main รวมผล
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    // ตรวจค่าเฉลี่ย, P95 แบบ nearest-rank และค่าสูงสุดจากตัวอย่างที่ทราบค่า
    LatencyMetrics metrics;
    metrics.record(1000);
    metrics.record(2000);
    metrics.record(3000);
    metrics.record(4000);
    metrics.record(5000);

    bool passed = true;
    passed = check(metrics.sample_count() == 5, "sample count") && passed;
    passed = check(nearly_equal(metrics.average_milliseconds(), 3.0),
                   "average includes every sample") && passed;
    passed = check(nearly_equal(metrics.percentile95_milliseconds(), 5.0),
                   "p95 uses the nearest-rank sample") && passed;
    passed = check(nearly_equal(metrics.maximum_milliseconds(), 5.0),
                   "maximum latency") && passed;

    // ตรวจว่าการรวมข้อมูลยังรักษาตัวอย่างไว้สำหรับคำนวณ aggregate P95
    LatencyMetrics other;
    other.record(6000);
    metrics.merge(other);
    passed = check(metrics.sample_count() == 6, "merge keeps every sample") && passed;
    passed = check(nearly_equal(metrics.average_milliseconds(), 3.5),
                   "merged average") && passed;
    passed = check(nearly_equal(metrics.percentile95_milliseconds(), 6.0),
                   "merged p95 is calculated from aggregate samples") && passed;

    // เมตริกที่ไม่มี Request ต้องคืนค่า latency เป็นศูนย์
    LatencyMetrics empty;
    passed = check(nearly_equal(empty.average_milliseconds(), 0.0),
                   "empty average") && passed;
    passed = check(nearly_equal(empty.percentile95_milliseconds(), 0.0),
                   "empty p95") && passed;
    passed = check(nearly_equal(empty.maximum_milliseconds(), 0.0),
                   "empty maximum") && passed;

    if (!passed) return 1;
    std::cout << "client_load_metrics_test: PASS\n";
    return 0;
}
