#include "common.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <mqueue.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

struct LoadOptions {
    int clients;
    int requests_per_client;
    Command command;
    int resource_id;

    LoadOptions()
        : clients(5),
          requests_per_client(10),
          command(Command::STATUS),
          resource_id(1) {}
};

struct ClientStats {
    long long successes;
    long long failures;
    long long total_latency_us;

    ClientStats()
        : successes(0), failures(0), total_latency_us(0) {}
};

struct ClientQueues {
    mqd_t request_queue;
    mqd_t response_queue;
    std::string response_queue_name;

    ClientQueues()
        : request_queue(static_cast<mqd_t>(-1)),
          response_queue(static_cast<mqd_t>(-1)) {}
};

std::string make_queue_name(int client_id) {
    std::ostringstream output;
    output << "/osproj_load_" << static_cast<long long>(getpid())
           << "_" << client_id;
    return output.str();
}

bool parse_options(int argc, char** argv, LoadOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);

        if (argument == "--clients" && index + 1 < argc) {
            if (!parse_integer(argv[++index], options.clients)) {
                return false;
            }
        } else if (argument == "--requests" && index + 1 < argc) {
            if (!parse_integer(argv[++index], options.requests_per_client)) {
                return false;
            }
        } else if (argument == "--command" && index + 1 < argc) {
            if (!parse_command(argv[++index], options.command)) {
                return false;
            }
        } else if (argument == "--resource" && index + 1 < argc) {
            if (!parse_integer(argv[++index], options.resource_id)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return options.clients > 0 &&
           options.requests_per_client > 0 &&
           valid_resource_id(options.resource_id) &&
           (options.command == Command::STATUS ||
            options.command == Command::RESERVE);
}

void record_latency(ClientStats& stats,
                    const std::chrono::steady_clock::time_point& start) {
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    stats.total_latency_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();
}

bool open_client_queues(int client_id, ClientQueues& queues,
                        ClientStats& stats) {
    queues.response_queue_name = make_queue_name(client_id);

    struct mq_attr response_attributes = {};
    response_attributes.mq_maxmsg = RESPONSE_QUEUE_MAX_MESSAGES;
    response_attributes.mq_msgsize = sizeof(ResponseMessage);

    mq_unlink(queues.response_queue_name.c_str());
    queues.response_queue = mq_open(
        queues.response_queue_name.c_str(),
        O_CREAT | O_RDONLY,
        0600,
        &response_attributes);

    if (queues.response_queue == static_cast<mqd_t>(-1)) {
        ++stats.failures;
        return false;
    }

    queues.request_queue = mq_open(REQUEST_QUEUE_NAME, O_WRONLY);
    if (queues.request_queue == static_cast<mqd_t>(-1)) {
        ++stats.failures;
        mq_close(queues.response_queue);
        mq_unlink(queues.response_queue_name.c_str());
        return false;
    }

    return true;
}

void close_client_queues(ClientQueues& queues) {
    if (queues.request_queue != static_cast<mqd_t>(-1)) {
        mq_close(queues.request_queue);
    }
    if (queues.response_queue != static_cast<mqd_t>(-1)) {
        mq_close(queues.response_queue);
    }
    mq_unlink(queues.response_queue_name.c_str());
}

void send_load_request(int client_id, const LoadOptions& options,
                       const ClientQueues& queues, ClientStats& stats) {
    RequestMessage request = {};
    request.command = static_cast<int>(options.command);
    request.client_id = client_id;
    request.resource_id = options.resource_id;
    copy_text(request.reply_queue, sizeof(request.reply_queue),
              queues.response_queue_name);

    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    const timespec send_deadline =
        deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    if (mq_timedsend(queues.request_queue,
                     reinterpret_cast<const char*>(&request),
                     sizeof(request), 0, &send_deadline) == -1) {
        ++stats.failures;
        record_latency(stats, start);
        return;
    }

    ResponseMessage response = {};
    const timespec receive_deadline =
        deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    if (mq_timedreceive(queues.response_queue,
                        reinterpret_cast<char*>(&response),
                        sizeof(response), 0,
                        &receive_deadline) == -1) {
        ++stats.failures;
        record_latency(stats, start);
        return;
    }

    record_latency(stats, start);
    if (response.success) {
        ++stats.successes;
    } else {
        ++stats.failures;
    }
}

void run_logical_client(int client_number, const LoadOptions& options,
                        ClientStats& stats) {
    const int client_id = LOAD_CLIENT_ID_OFFSET + client_number;
    ClientQueues queues;
    if (!open_client_queues(client_id, queues, stats)) {
        return;
    }

    for (int request_number = 0;
         request_number < options.requests_per_client;
         ++request_number) {
        send_load_request(client_id, options, queues, stats);
    }

    close_client_queues(queues);
}

int main(int argc, char** argv) {
    LoadOptions options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " --clients N --requests N "
                     "--command STATUS|RESERVE --resource N"
                  << '\n';
        return 1;
    }

    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    std::vector<std::thread> clients;
    clients.reserve(static_cast<std::size_t>(options.clients));
    std::vector<ClientStats> client_stats(
        static_cast<std::size_t>(options.clients));
    for (int client = 1; client <= options.clients; ++client) {
        clients.push_back(std::thread(
            run_logical_client,
            client,
            std::cref(options),
            std::ref(client_stats[static_cast<std::size_t>(client - 1)])));
    }

    for (std::size_t index = 0; index < clients.size(); ++index) {
        clients[index].join();
    }

    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    const long long elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();
    long long success_count = 0;
    long long failure_count = 0;
    long long total_latency_us = 0;
    for (std::size_t index = 0; index < client_stats.size(); ++index) {
        success_count += client_stats[index].successes;
        failure_count += client_stats[index].failures;
        total_latency_us += client_stats[index].total_latency_us;
    }
    const long long total_requests = success_count + failure_count;

    const double elapsed_seconds =
        static_cast<double>(elapsed_us) / 1000000.0;
    double throughput = 0.0;
    if (elapsed_seconds > 0.0) {
        throughput = static_cast<double>(total_requests) / elapsed_seconds;
    }

    double average_latency_ms = 0.0;
    if (total_requests > 0) {
        average_latency_ms =
            static_cast<double>(total_latency_us) /
            static_cast<double>(total_requests) / 1000.0;
    }

    std::cout << "clients=" << options.clients
              << " requests_per_client=" << options.requests_per_client
              << " total_requests=" << total_requests
              << " success=" << success_count
              << " failure_or_timeout=" << failure_count
              << " elapsed_ms=" << elapsed_us / 1000.0
              << " throughput_req_per_sec=" << throughput
              << " average_latency_ms=" << average_latency_ms
              << '\n';

    if (failure_count == 0) {
        return 0;
    }
    return 2;
}
