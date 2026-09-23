#ifndef LOAD_WORKLOAD_HPP
#define LOAD_WORKLOAD_HPP

#include "common.hpp"

#include <string>

// รูปแบบการเลือก Resource สำหรับ Logical Client ใน Load Test
enum class LoadWorkload { SameResource, RoundRobin };

// คืนชื่อรูปแบบ Workload ที่ใช้แสดงในผลสรุปและรายงาน
inline const char* load_workload_name(LoadWorkload workload) {
    if (workload == LoadWorkload::SameResource) return "same-resource";
    return "round-robin";
}

// รับ alias ที่รองรับและแปลงเป็นรูปแบบ Workload ภายใน
inline bool parse_load_workload(const std::string& token,
                                LoadWorkload& workload) {
    if (token == "same" || token == "same-resource" || token == "fixed") {
        workload = LoadWorkload::SameResource;
        return true;
    }
    if (token == "round-robin" || token == "round_robin") {
        workload = LoadWorkload::RoundRobin;
        return true;
    }
    return false;
}

// เลือก Resource เดิมให้ทุก Client หรือวน Resource ID ตั้งแต่ 1 ถึง 20
inline int load_resource_for_client(int client_number,
                                    LoadWorkload workload,
                                    int same_resource_id) {
    if (workload == LoadWorkload::SameResource) return same_resource_id;
    return ((client_number - 1) % RESOURCE_COUNT) + 1;
}

#endif
