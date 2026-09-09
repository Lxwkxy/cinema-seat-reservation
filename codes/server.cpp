#include "common.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <mqueue.h>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

std::atomic<bool> running(true);
std::mutex log_mutex;
std::array<std::mutex, RESOURCE_COUNT> resource_mutexes;
std::vector<ResourceRecord> resources;
bool synchronization_enabled = true;
bool random_delay_enabled = false;

void handle_signal(int) {
    running.store(false);
}

long long now_milliseconds() {
    const std::chrono::milliseconds value =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    return value.count();
}

void log_message(int worker_id, const RequestMessage& request,
                 const std::string& message) {
    std::lock_guard<std::mutex> guard(log_mutex);
    std::cout << "[t=" << now_milliseconds() << "] "
              << "[Worker-" << worker_id << "] "
              << "[Client-" << request.client_id << "] "
              << "[" << command_name(static_cast<Command>(request.command))
              << "] " << message << '\n';
}

void add_random_delay() {
    if (!random_delay_enabled) {
        return;
    }

    static thread_local std::mt19937 generator(
        static_cast<unsigned>(std::chrono::high_resolution_clock::now()
                                  .time_since_epoch()
                                  .count()));
    std::uniform_int_distribution<int> distribution(
        MIN_RANDOM_DELAY_MS, MAX_RANDOM_DELAY_MS);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(distribution(generator)));
}

ResponseMessage make_response(const RequestMessage& request, bool success,
                              int resource_id, int owner_id,
                              const std::string& text) {
    ResponseMessage response = {};
    response.command = request.command;
    response.client_id = request.client_id;
    if (success) {
        response.success = 1;
    } else {
        response.success = 0;
    }
    response.resource_id = resource_id;
    response.owner_id = owner_id;
    copy_text(response.text, sizeof(response.text), text);
    return response;
}

ResourceRecord& resource_at(int resource_id) {
    return resources[static_cast<std::size_t>(resource_id - 1)];
}

ResponseMessage handle_list(const RequestMessage& request, int worker_id) {
    std::ostringstream output;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const ResourceRecord& resource = resources[index];
        output << resource.id << ":";
        if (resource.is_reserved()) {
            output << "RESERVED(Client-" << resource.owner_id << ")";
        } else {
            output << "AVAILABLE";
        }
        if (index + 1 < resources.size()) {
            output << " ";
        }
    }

    log_message(worker_id, request, "listed all resources");
    return make_response(request, true, -1, -1, output.str());
}

ResponseMessage handle_status(const RequestMessage& request, int worker_id,
                              ResourceRecord& resource) {
    std::ostringstream output;
    output << "Resource " << resource.id << ": ";
    if (resource.is_reserved()) {
        output << "RESERVED, owner=Client-" << resource.owner_id;
    } else {
        output << "AVAILABLE";
    }

    log_message(worker_id, request, "read resource status");
    return make_response(request, true, resource.id, resource.owner_id,
                         output.str());
}

ResponseMessage handle_reserve(const RequestMessage& request, int worker_id,
                               ResourceRecord& resource) {
    log_message(worker_id, request, "checking availability");
    if (resource.is_reserved()) {
        return make_response(request, false, resource.id, resource.owner_id,
                             "Resource is already reserved");
    }

    log_message(worker_id, request, "resource is AVAILABLE");
    add_random_delay();

    // Intentionally no second check. This exposes the race window
    // when the server runs with --sync off.
    resource.reserve_for(request.client_id);

    log_message(worker_id, request, "reserved resource");
    return make_response(request, true, resource.id, resource.owner_id,
                         "Reservation successful");
}

ResponseMessage handle_cancel(const RequestMessage& request, int worker_id,
                              ResourceRecord& resource) {
    if (!resource.is_reserved()) {
        return make_response(request, false, resource.id, -1,
                             "Resource is not reserved");
    }
    if (resource.owner_id != request.client_id) {
        return make_response(request, false, resource.id, resource.owner_id,
                             "Only the owner can cancel this reservation");
    }

    resource.release();
    log_message(worker_id, request, "cancelled reservation");
    return make_response(request, true, resource.id, -1,
                         "Cancellation successful");
}

ResponseMessage process_request_core(const RequestMessage& request,
                                     int worker_id) {
    const Command command = static_cast<Command>(request.command);

    switch (command) {
        case Command::LIST:
            return handle_list(request, worker_id);

        case Command::QUIT:
            log_message(worker_id, request, "client requested disconnect");
            return make_response(request, true, -1, -1,
                                 "Client disconnected");

        case Command::STATUS:
        case Command::RESERVE:
        case Command::CANCEL:
            break;
    }

    if (!valid_resource_id(request.resource_id)) {
        log_message(worker_id, request, "invalid resource id");
        std::ostringstream error_message;
        error_message << "Resource ID must be between 1 and "
                      << RESOURCE_COUNT;
        return make_response(request, false, request.resource_id, -1,
                             error_message.str());
    }

    ResourceRecord& resource = resource_at(request.resource_id);
    switch (command) {
        case Command::STATUS:
            return handle_status(request, worker_id, resource);
        case Command::RESERVE:
            return handle_reserve(request, worker_id, resource);
        case Command::CANCEL:
            return handle_cancel(request, worker_id, resource);
        default:
            return make_response(request, false, -1, -1,
                                 "Unknown command");
    }
}

ResponseMessage process_request(const RequestMessage& request, int worker_id) {
    if (!synchronization_enabled) {
        return process_request_core(request, worker_id);
    }

    const Command command = static_cast<Command>(request.command);

    if (command == Command::LIST) {
        std::array<std::unique_lock<std::mutex>, RESOURCE_COUNT> guards;
        for (std::size_t index = 0; index < resource_mutexes.size(); ++index) {
            guards[index] = std::unique_lock<std::mutex>(resource_mutexes[index]);
        }

        log_message(worker_id, request, "entering critical section");
        ResponseMessage response = process_request_core(request, worker_id);
        log_message(worker_id, request, "leaving critical section");
        return response;
    }

    if (!command_requires_resource(command) ||
        !valid_resource_id(request.resource_id)) {
        return process_request_core(request, worker_id);
    }

    std::lock_guard<std::mutex> guard(
        resource_mutexes[static_cast<std::size_t>(request.resource_id - 1)]);
    log_message(worker_id, request, "entering critical section");
    ResponseMessage response = process_request_core(request, worker_id);
    log_message(worker_id, request, "leaving critical section");
    return response;
}

void send_response(const RequestMessage& request,
                   const ResponseMessage& response) {
    mqd_t response_queue = mq_open(request.reply_queue, O_WRONLY);
    if (response_queue == static_cast<mqd_t>(-1)) {
        std::lock_guard<std::mutex> guard(log_mutex);
        std::cerr << "mq_open response queue failed for "
                  << request.reply_queue << ": "
                  << std::strerror(errno) << '\n';
        return;
    }

    const timespec deadline =
        deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    if (mq_timedsend(response_queue,
                     reinterpret_cast<const char*>(&response),
                     sizeof(response), 0, &deadline) == -1) {
        std::lock_guard<std::mutex> guard(log_mutex);
        std::cerr << "mq_send response failed: "
                  << std::strerror(errno) << '\n';
    }

    mq_close(response_queue);
}

void worker_loop(int worker_id, mqd_t request_queue) {
    while (running.load()) {
        RequestMessage request = {};
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 1;

        const ssize_t received = mq_timedreceive(
            request_queue,
            reinterpret_cast<char*>(&request),
            sizeof(request),
            0,
            &deadline);

        if (received == -1) {
            if (errno == ETIMEDOUT || errno == EINTR) {
                continue;
            }
            if (!running.load()) {
                break;
            }
            std::lock_guard<std::mutex> guard(log_mutex);
            std::cerr << "Worker-" << worker_id
                      << " mq_receive failed: "
                      << std::strerror(errno) << '\n';
            continue;
        }

        if (received != static_cast<ssize_t>(sizeof(request))) {
            log_message(worker_id, request, "received malformed message");
            continue;
        }

        log_message(worker_id, request, "received request");
        const ResponseMessage response = process_request(request, worker_id);
        send_response(request, response);
    }

}

bool parse_bool_value(const std::string& value, bool& result) {
    if (value == "on" || value == "true" || value == "1") {
        result = true;
        return true;
    }
    if (value == "off" || value == "false" || value == "0") {
        result = false;
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    int worker_count = 3;

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);

        if (argument == "--workers" && index + 1 < argc) {
            int parsed_workers = 0;
            if (!parse_integer(argv[++index], parsed_workers)) {
                std::cerr << "Invalid --workers value\n";
                return 1;
            }
            worker_count = std::max(1, parsed_workers);
        } else if (argument == "--sync" && index + 1 < argc) {
            if (!parse_bool_value(argv[++index], synchronization_enabled)) {
                std::cerr << "Invalid --sync value\n";
                return 1;
            }
        } else if (argument == "--delay" && index + 1 < argc) {
            if (!parse_bool_value(argv[++index], random_delay_enabled)) {
                std::cerr << "Invalid --delay value\n";
                return 1;
            }
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--workers N] [--sync on|off] [--delay on|off]"
                      << '\n';
            return 1;
        }
    }

    resources.reserve(RESOURCE_COUNT);
    for (int id = 1; id <= RESOURCE_COUNT; ++id) {
        ResourceRecord resource = {};
        resource.id = id;
        resource.owner_id = -1;
        resources.push_back(resource);
    }

    struct mq_attr attributes = {};
    attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = sizeof(RequestMessage);

    mq_unlink(REQUEST_QUEUE_NAME);
    mqd_t request_queue = mq_open(
        REQUEST_QUEUE_NAME,
        O_CREAT | O_RDONLY,
        0666,
        &attributes);

    if (request_queue == static_cast<mqd_t>(-1)) {
        std::cerr << "Cannot create request queue: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const char* sync_status = "off";
    if (synchronization_enabled) {
        sync_status = "on";
    }

    const char* delay_status = "off";
    if (random_delay_enabled) {
        delay_status = "on";
    }

    std::cout << "Server started: workers=" << worker_count
              << ", sync=" << sync_status
              << ", random_delay=" << delay_status
              << '\n';
    std::cout << "Request queue: " << REQUEST_QUEUE_NAME << '\n';

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int id = 1; id <= worker_count; ++id) {
        workers.push_back(std::thread(worker_loop, id, request_queue));
    }

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index].join();
    }

    mq_close(request_queue);
    mq_unlink(REQUEST_QUEUE_NAME);
    std::cout << "Server stopped and request queue removed" << '\n';
    return 0;
}
