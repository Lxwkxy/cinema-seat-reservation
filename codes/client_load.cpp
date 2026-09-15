#include "message_queue.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

struct LoadOptions {
    int clients = 5;
    int requests_per_client = 10;
    Command command = Command::STATUS;
    int resource_id = 1;
};

struct ClientStats {
    long long succeeded = 0;
    long long rejected = 0;
    long long timeouts = 0;
    long long transport_errors = 0;
    long long setup_errors = 0;
    long long total_latency_us = 0;

    long long attempted() const {
        return succeeded + rejected + timeouts + transport_errors;
    }

    void merge(const ClientStats& other) {
        succeeded += other.succeeded;
        rejected += other.rejected;
        timeouts += other.timeouts;
        transport_errors += other.transport_errors;
        setup_errors += other.setup_errors;
        total_latency_us += other.total_latency_us;
    }
};

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
        } else {
            return false;
        }
    }
    return options.clients > 0 &&
           options.clients <= INT_MAX - LOAD_CLIENT_ID_OFFSET &&
           options.requests_per_client > 0 &&
           valid_resource_id(options.resource_id) &&
           (options.command == Command::STATUS || options.command == Command::RESERVE);
}

void run_logical_client(int client_number, const LoadOptions& options,
                        ClientStats& stats) {
    ClientConnection connection;
    if (!connection.open(LOAD_CLIENT_ID_OFFSET + client_number, "/osproj_load_")) {
        ++stats.setup_errors;
        return;
    }

    for (int request = 0; request < options.requests_per_client; ++request) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = connection.send(options.command, options.resource_id);
        stats.total_latency_us +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();

        switch (result.outcome) {
            case RequestOutcome::Succeeded: ++stats.succeeded; break;
            case RequestOutcome::Rejected: ++stats.rejected; break;
            case RequestOutcome::Timeout: ++stats.timeouts; break;
            case RequestOutcome::TransportError: ++stats.transport_errors; break;
        }
        // Stop after a transport failure; pending replies belong to this session.
        if (!result.received_response()) break;
    }
}

void print_stats(const LoadOptions& options, const ClientStats& stats,
                 long long elapsed_us) {
    const long long planned =
        static_cast<long long>(options.clients) * options.requests_per_client;
    const long long attempted = stats.attempted();
    double throughput = 0.0;
    if (elapsed_us > 0)
        throughput = static_cast<double>(attempted) * 1000000.0 / elapsed_us;
    double average_latency_ms = 0.0;
    if (attempted > 0)
        average_latency_ms = static_cast<double>(stats.total_latency_us) /
                             attempted / 1000.0;

    std::cout << "clients=" << options.clients
              << " requests_per_client=" << options.requests_per_client
              << " planned_requests=" << planned
              << " total_requests=" << attempted
              << " skipped_requests=" << planned - attempted
              << " success=" << stats.succeeded
              << " rejected=" << stats.rejected
              << " timeouts=" << stats.timeouts
              << " transport_errors=" << stats.transport_errors
              << " setup_errors=" << stats.setup_errors
              << " elapsed_ms=" << elapsed_us / 1000.0
              << " throughput_req_per_sec=" << throughput
              << " average_latency_ms=" << average_latency_ms << '\n';
}

int main(int argc, char** argv) {
    LoadOptions options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " --clients N --requests N --command STATUS|RESERVE --resource N\n";
        return 1;
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

    if (total.setup_errors || total.timeouts || total.transport_errors) return 2;
    if (total.rejected) return 3;
    return 0;
}
