#include "message_queue.hpp"
#include "load_metrics.hpp"
#include "load_workload.hpp"

#include <chrono>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// การตั้งค่าจำนวน Logical Client, Request และรูปแบบ Workload ของ Load Test
struct LoadOptions {
    int clients = 5;
    int requests_per_client = 10;
    Command command = Command::STATUS;
    int resource_id = 1;
    LoadWorkload workload = LoadWorkload::SameResource;
    std::string report_path;
    std::string experiment = "manual";
};

// สถิติและรายละเอียดผลลัพธ์ที่สะสมแยกตาม Logical Client
struct ClientStats {
    long long succeeded = 0;
    long long rejected = 0;
    long long timeouts = 0;
    long long transport_errors = 0;
    long long setup_errors = 0;
    LatencyMetrics latency;
    std::string detail;

    // คำนวณจำนวน Request ที่เริ่มส่งและได้ผลลัพธ์กลับมา
    long long attempted() const {
        return succeeded + rejected + timeouts + transport_errors;
    }

    // รวมสถิติของ Logical Client หนึ่งตัวเข้ากับยอดรวม
    void merge(const ClientStats& other) {
        succeeded += other.succeeded;
        rejected += other.rejected;
        timeouts += other.timeouts;
        transport_errors += other.transport_errors;
        setup_errors += other.setup_errors;
        latency.merge(other.latency);
    }
};

// แยกวิเคราะห์และตรวจสอบตัวเลือกของ Load Test
bool parse_options(int argc, char** argv, LoadOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (index + 1 >= argc) return false;
        const std::string value(argv[++index]);
        if (argument == "--clients") {
            if (!parse_integer(value, options.clients)) return false;
        } else if (argument == "--requests") {
            if (!parse_integer(value, options.requests_per_client)) return false;
        } else if (argument == "--command") {
            if (!parse_command(value, options.command)) return false;
        } else if (argument == "--resource") {
            if (!parse_integer(value, options.resource_id)) return false;
        } else if (argument == "--workload") {
            if (!parse_load_workload(value, options.workload)) return false;
        } else if (argument == "--report") {
            options.report_path = value;
        } else if (argument == "--experiment") {
            options.experiment = value;
        } else {
            return false;
        }
    }
    return options.clients > 0 &&
           options.clients <= INT_MAX - LOAD_CLIENT_ID_OFFSET &&
           options.requests_per_client > 0 &&
           (options.workload == LoadWorkload::RoundRobin ||
            valid_resource_id(options.resource_id)) &&
           (options.command == Command::STATUS || options.command == Command::RESERVE);
}

// จำลอง Client หนึ่งตัวที่ส่ง Request ตามจำนวนที่กำหนด
void run_logical_client(int client_number, const LoadOptions& options,
                        ClientStats& stats) {
    ClientConnection connection;
    if (!connection.open(LOAD_CLIENT_ID_OFFSET + client_number, "/osproj_load_")) {
        ++stats.setup_errors;
        stats.detail = std::strerror(errno);
        return;
    }

    const int resource_id = load_resource_for_client(client_number,
                                                     options.workload,
                                                     options.resource_id);

    for (int request = 0; request < options.requests_per_client; ++request) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = connection.send(options.command, resource_id);
        const auto latency_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
        stats.latency.record(latency_us);
        if (!options.report_path.empty()) {
            if (result.received_response()) stats.detail = result.response.text;
            else stats.detail = std::strerror(result.error_number);
        }

        switch (result.outcome) {
            case RequestOutcome::Succeeded: ++stats.succeeded; break;
            case RequestOutcome::Rejected: ++stats.rejected; break;
            case RequestOutcome::Timeout: ++stats.timeouts; break;
            case RequestOutcome::TransportError: ++stats.transport_errors; break;
        }
        // หยุดเมื่อเกิดข้อผิดพลาดในการสื่อสาร เพราะ Response ที่ค้างอยู่เป็นของ Session นี้
        if (!result.received_response()) break;
    }
}

// คำนวณและพิมพ์ Metrics ของ Load Test
void print_stats(const LoadOptions& options, const ClientStats& stats,
                 long long elapsed_us) {
    const long long planned =
        static_cast<long long>(options.clients) * options.requests_per_client;
    const long long attempted = stats.attempted();
    const long long skipped = planned - attempted;
    double throughput = 0.0;
    if (elapsed_us > 0)
        throughput = static_cast<double>(attempted) * 1000000.0 / elapsed_us;

    std::cout << "workload=" << load_workload_name(options.workload)
              << " clients=" << options.clients
              << " requests_per_client=" << options.requests_per_client
              << " planned_requests=" << planned
              << " attempted_requests=" << attempted
              << " total_requests=" << attempted
              << " skipped_requests=" << skipped
              << " success=" << stats.succeeded
              << " rejected=" << stats.rejected
              << " timeouts=" << stats.timeouts
              << " transport_errors=" << stats.transport_errors
              << " setup_errors=" << stats.setup_errors
              << " elapsed_ms=" << elapsed_us / 1000.0
              << " throughput_req_per_sec=" << throughput
              << " average_latency_ms=" << stats.latency.average_milliseconds()
              << " p95_latency_ms=" << stats.latency.percentile95_milliseconds()
              << " max_latency_ms=" << stats.latency.maximum_milliseconds()
              << " latency_scope=attempted_requests_including_failures" << '\n';
}

// เขียนผลราย Client และยอดรวม รวมถึง latency ลงในรายงานที่ผู้ใช้ระบุ
void write_report(std::ostream& output, const LoadOptions& options,
                  const std::vector<ClientStats>& clients, int started_clients,
                  const ClientStats& total, long long elapsed_us) {
    output << "Concurrent reservation result report\n"
           << "Experiment: " << options.experiment << '\n'
           << "Command: " << command_name(options.command) << '\n'
           << "Workload: " << load_workload_name(options.workload) << '\n';
    if (options.workload == LoadWorkload::SameResource)
        output << "Target seat: " << options.resource_id << '\n';
    else output << "Target seats: 1-" << RESOURCE_COUNT << " (round-robin)\n";
    output << "Requests per client: " << options.requests_per_client << '\n'
           << "Client labels map to server IDs: client-N = Client-"
           << LOAD_CLIENT_ID_OFFSET << " + N\n\n";

    output << "Client       | Seat | Result\n"
           << "-------------|------|------------------------------------------------------------\n";
    for (int index = 0; index < options.clients; ++index) {
        output << std::left << std::setw(13) << ("client-" + std::to_string(index + 1))
               << "| " << std::setw(5)
               << load_resource_for_client(index + 1, options.workload, options.resource_id)
               << "| ";
        if (index >= started_clients) {
            output << "SETUP ERROR: Client thread not started; skipped="
                   << options.requests_per_client << '\n';
            continue;
        }
        const auto& stats = clients[static_cast<std::size_t>(index)];
        if (stats.setup_errors) output << "SETUP ERROR: " << stats.detail;
        else if (options.requests_per_client == 1) {
            if (stats.succeeded) output << "SUCCESS: ";
            else if (stats.rejected) output << "FAILED (rejected): ";
            else if (stats.timeouts) output << "TIMEOUT (server outcome unknown): ";
            else output << "TRANSPORT ERROR: ";
            output << stats.detail;
        } else {
            output << "success=" << stats.succeeded << ", rejected=" << stats.rejected
                   << ", timeouts=" << stats.timeouts
                   << ", transport_errors=" << stats.transport_errors
                   << ", skipped=" << options.requests_per_client - stats.attempted()
                   << ", avg_ms=" << stats.latency.average_milliseconds()
                   << "; last result: " << stats.detail;
        }
        output << '\n';
    }
    const long long planned = static_cast<long long>(options.clients) * options.requests_per_client;
    output << "\nSuccessful requests: " << total.succeeded << '/' << planned
           << "\nRejected requests: " << total.rejected << '/' << planned
           << "\nTimeouts (server outcome unknown): " << total.timeouts
           << "\nTransport errors: " << total.transport_errors
           << "\nClient setup errors: " << total.setup_errors
           << "\nAttempted requests: " << total.attempted() << '/' << planned
           << "\nSkipped requests: " << planned - total.attempted() << '\n';
    if (options.command == Command::RESERVE) {
        output << "Successful reservations: " << total.succeeded << '/' << planned
               << "\nFailed reservations (explicitly rejected): " << total.rejected << '/' << planned << '\n';
    }
    double throughput = 0.0;
    if (elapsed_us > 0) throughput = total.attempted() * 1000000.0 / elapsed_us;
    output << "Elapsed time (ms): " << elapsed_us / 1000.0
           << "\nThroughput (requests/s): " << throughput
           << "\nAverage latency (ms): " << total.latency.average_milliseconds()
           << "\nP95 latency (ms): " << total.latency.percentile95_milliseconds()
           << "\nMaximum latency (ms): " << total.latency.maximum_milliseconds()
           << "\nLatency includes attempted requests, including failures; excludes setup errors.\n";
}

// จุดเริ่มต้นของ Load Test: สร้าง Logical Client และสรุปผลการทดสอบ
int main(int argc, char** argv) {
    LoadOptions options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " --clients N --requests N --command STATUS|RESERVE"
                     " [--resource N] [--workload same|round-robin]"
                     " [--report PATH] [--experiment NAME]\n";
        return 1;
    }

    std::ofstream report;
    if (!options.report_path.empty()) {
        report.open(options.report_path);
        if (!report) {
            std::cerr << "Cannot open report: " << options.report_path << '\n';
            return 1;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> clients;
    std::vector<ClientStats> client_stats;
    int started_clients = 0;
    try {
        clients.reserve(static_cast<std::size_t>(options.clients));
        client_stats.resize(static_cast<std::size_t>(options.clients));
        for (int index = 0; index < options.clients; ++index) {
            clients.emplace_back(run_logical_client, index + 1, std::cref(options),
                                 std::ref(client_stats[static_cast<std::size_t>(index)]));
            ++started_clients;
        }
    } catch (const std::exception& error) {
        std::cerr << "Cannot start all clients: " << error.what() << '\n';
    }
    for (auto& client : clients) client.join();

    ClientStats total;
    total.setup_errors = options.clients - started_clients;
    for (const auto& stats : client_stats) total.merge(stats);
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    print_stats(options, total, elapsed_us);
    if (report.is_open()) {
        write_report(report, options, client_stats, started_clients, total, elapsed_us);
        report.close();
        if (!report) {
            std::cerr << "Cannot write report: " << options.report_path << '\n';
            return 1;
        }
    }

    if (total.setup_errors || total.timeouts || total.transport_errors) return 2;
    if (total.rejected) return 3;
    return 0;
}
