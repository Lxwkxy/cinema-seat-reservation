#include "load_workload.hpp"

#include <iostream>

namespace {

// รายงานชื่อกรณีที่ไม่ผ่านและคืนสถานะให้ main รวมผล
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    bool passed = true;
    // ตรวจชื่อ Workload ที่รองรับและปฏิเสธชื่อที่ไม่รู้จัก
    LoadWorkload workload = LoadWorkload::RoundRobin;
    passed = check(parse_load_workload("same", workload),
                   "parse same workload") && passed;
    passed = check(workload == LoadWorkload::SameResource,
                   "same workload value") && passed;
    passed = check(parse_load_workload("round-robin", workload),
                   "parse round-robin workload") && passed;
    passed = check(workload == LoadWorkload::RoundRobin,
                   "round-robin workload value") && passed;
    passed = check(!parse_load_workload("unknown", workload),
                   "reject unknown workload") && passed;

    // SameResource ต้องคง Resource ID ที่กำหนดให้กับทุก Client
    passed = check(load_resource_for_client(1, LoadWorkload::SameResource, 10) == 10,
                   "same workload keeps resource") && passed;
    passed = check(load_resource_for_client(7, LoadWorkload::SameResource, 10) == 10,
                   "same workload keeps resource for every request") && passed;
    // Round-robin วน Resource 1-20 แล้วเริ่มรอบใหม่ที่ Resource 1
    passed = check(load_resource_for_client(1, LoadWorkload::RoundRobin, 10) == 1,
                   "round-robin starts at resource one") && passed;
    passed = check(load_resource_for_client(20, LoadWorkload::RoundRobin, 10) == 20,
                   "round-robin reaches resource twenty") && passed;
    passed = check(load_resource_for_client(21, LoadWorkload::RoundRobin, 10) == 1,
                   "round-robin wraps after resource twenty") && passed;

    if (!passed) return 1;
    std::cout << "client_load_workload_test: PASS\n";
    return 0;
}
