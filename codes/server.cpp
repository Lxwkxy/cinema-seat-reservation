#include "message_queue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

struct ServerOptions {
    int workers = 3;
    bool synchronize = true;
    bool delay = false;
    bool verbose = false;
};

struct ResourceState {
    std::mutex mutex;
    // Atomic accesses keep the --sync off demonstration defined in C++.
    // The availability check and update remain separate, so double booking
    // is still possible without the resource mutex.
    std::atomic<int> owner{NO_OWNER};
};

using ResourceSnapshot = std::array<ResourceRecord, RESOURCE_COUNT>;

ServerOptions options;
std::array<ResourceState, RESOURCE_COUNT> resources;
std::mutex log_mutex;
std::atomic<unsigned long long> log_sequence{0};
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Signal handling requires lock-free bool");
std::atomic<bool> stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true);
}

struct LogEvent {
    unsigned long long sequence = 0;
    long long timestamp = 0;
    const char* message = nullptr;
};

class RequestTrace {
public:
    void record(const char* message) {
        if (!options.verbose) return;
        auto& event = events_[count_++];
        event.sequence = ++log_sequence;
        event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        event.message = message;
    }

    void print(int worker_id, const RequestMessage& request) const {
        if (!options.verbose) return;
        std::lock_guard<std::mutex> guard(log_mutex);
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& event = events_[index];
            std::cout << "[seq=" << event.sequence << "] [t=" << event.timestamp
                      << "] [Worker-" << worker_id << "] [Client-" << request.client_id
                      << "] [" << command_name(static_cast<Command>(request.command))
                      << "] [Resource-";
            if (static_cast<Command>(request.command) == Command::LIST)
                std::cout << "ALL";
            else if (static_cast<Command>(request.command) == Command::QUIT)
                std::cout << "NONE";
            else
                std::cout << request.resource_id;
            std::cout << "] " << event.message << '\n';
        }
        std::cout.flush();
    }

private:
    // Each request records at most five events; no allocation under a resource lock.
    std::array<LogEvent, 5> events_;
    std::size_t count_ = 0;
};

class CriticalSectionLog {
public:
    CriticalSectionLog(RequestTrace& trace, bool locked)
        : trace_(trace), locked_(locked) {
        if (locked_) trace_.record("entering critical section");
    }
    ~CriticalSectionLog() {
        if (locked_) trace_.record("leaving critical section");
    }
    CriticalSectionLog(const CriticalSectionLog&) = delete;
    CriticalSectionLog& operator=(const CriticalSectionLog&) = delete;

private:
    RequestTrace& trace_;
    bool locked_;
};

void add_random_delay() {
    if (!options.delay) return;
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(
        MIN_RANDOM_DELAY_MS, MAX_RANDOM_DELAY_MS);
    std::this_thread::sleep_for(std::chrono::milliseconds(distribution(generator)));
}

ResponseMessage make_response(const RequestMessage& request, bool success,
                              int owner_id, const std::string& text) {
    ResponseMessage response = {};
    response.command = request.command;
    response.client_id = request.client_id;
    response.success = static_cast<int>(success);
    response.resource_id = request.resource_id;
    response.owner_id = owner_id;
    copy_text(response.text, sizeof(response.text), text);
    return response;
}

ResourceSnapshot snapshot_resources(RequestTrace& trace) {
    // Acquire locks in ascending order for a consistent snapshot.
    std::array<std::unique_lock<std::mutex>, RESOURCE_COUNT> guards;
    if (options.synchronize) {
        for (std::size_t index = 0; index < resources.size(); ++index)
            guards[index] = std::unique_lock<std::mutex>(resources[index].mutex);
    }
    // Construct after the guards so the exit event is captured before unlocking.
    CriticalSectionLog critical_section(trace, options.synchronize);
    ResourceSnapshot snapshot = {};
    for (std::size_t index = 0; index < resources.size(); ++index) {
        snapshot[index].id = static_cast<int>(index + 1);
        snapshot[index].owner_id = resources[index].owner.load();
    }
    return snapshot;
}

ResponseMessage handle_list(const RequestMessage& request, RequestTrace& trace) {
    const auto snapshot = snapshot_resources(trace);
    // Formatting happens after all resource locks have been released.
    std::ostringstream output;
    for (const auto& resource : snapshot) {
        if (resource.id > 1) output << ' ';
        output << resource.id << ':';
        if (resource.is_reserved())
            output << "RESERVED(Client-" << resource.owner_id << ')';
        else
            output << "AVAILABLE";
    }
    return make_response(request, true, NO_OWNER, output.str());
}

struct ResourceResult {
    bool success = false;
    int owner = NO_OWNER;
    const char* message = "";
};

ResourceResult update_resource(const RequestMessage& request, RequestTrace& trace) {
    auto& resource = resources[static_cast<std::size_t>(request.resource_id - 1)];
    std::unique_lock<std::mutex> guard(resource.mutex, std::defer_lock);
    if (options.synchronize) guard.lock();
    CriticalSectionLog critical_section(trace, guard.owns_lock());

    ResourceResult result;
    result.owner = resource.owner.load();
    if (result.owner == NO_OWNER) trace.record("check resource: AVAILABLE");
    else trace.record("check resource: RESERVED");
    switch (static_cast<Command>(request.command)) {
        case Command::STATUS:
            result.success = true;
            break;
        case Command::RESERVE:
            result.message = "Resource is already reserved";
            if (result.owner != NO_OWNER) break;
            // Keep the delay between check and update for the race experiment.
            add_random_delay();
            resource.owner.store(request.client_id);
            result.owner = request.client_id;
            result.success = true;
            result.message = "Reservation successful";
            break;
        case Command::CANCEL:
            result.message = "Resource is not reserved";
            if (result.owner == NO_OWNER) break;
            result.message = "Only the owner can cancel this reservation";
            if (result.owner != request.client_id) break;
            resource.owner.store(NO_OWNER);
            result.owner = NO_OWNER;
            result.success = true;
            result.message = "Cancellation successful";
            break;
        default:
            result.message = "Unknown command";
            break;
    }
    return result;
}

ResponseMessage process_request(const RequestMessage& request, RequestTrace& trace) {
    const auto command = static_cast<Command>(request.command);
    if (request.client_id <= 0)
        return make_response(request, false, NO_OWNER, "Client ID must be positive");
    if (command == Command::LIST) return handle_list(request, trace);
    if (command == Command::QUIT)
        return make_response(request, true, NO_OWNER, "Client disconnected");
    if (!command_requires_resource(command))
        return make_response(request, false, NO_OWNER, "Unknown command");
    if (!valid_resource_id(request.resource_id))
        return make_response(request, false, NO_OWNER,
                             "Resource ID must be between 1 and " +
                             std::to_string(RESOURCE_COUNT));

    const auto result = update_resource(request, trace);
    if (command == Command::STATUS) {
        std::ostringstream output;
        output << "Resource " << request.resource_id << ": ";
        if (result.owner == NO_OWNER) output << "AVAILABLE";
        else output << "RESERVED, owner=Client-" << result.owner;
        return make_response(request, true, result.owner, output.str());
    }
    return make_response(request, result.success, result.owner, result.message);
}

void report_queue_error(const char* operation, int error) {
    std::lock_guard<std::mutex> guard(log_mutex);
    std::cerr << operation << ": " << std::strerror(error) << '\n';
}

void send_response(const RequestMessage& request, const ResponseMessage& response) {
    MessageQueue queue;
    if (!queue.open(request.reply_queue, O_WRONLY)) {
        report_queue_error("Cannot open response queue", errno);
        return;
    }
    const auto deadline = deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    int sent;
    do {
        sent = mq_timedsend(queue.descriptor(),
                            reinterpret_cast<const char*>(&response),
                            sizeof(response), 0, &deadline);
    } while (sent == -1 && errno == EINTR && !stop_requested);
    if (sent == -1) report_queue_error("Cannot send response", errno);
}

void worker_loop(int worker_id, mqd_t request_queue) {
    while (!stop_requested) {
        RequestMessage request = {};
        const auto deadline = deadline_after_seconds(1);
        const auto received = mq_timedreceive(
            request_queue, reinterpret_cast<char*>(&request),
            sizeof(request), nullptr, &deadline);
        if (received == -1) {
            const int error = errno;
            if (error == ETIMEDOUT || error == EINTR) continue;
            report_queue_error("Cannot receive request", error);
            return;
        }
        if (received != static_cast<ssize_t>(sizeof(request)) ||
            request.reply_queue[0] != '/' ||
            std::memchr(request.reply_queue, '\0', sizeof(request.reply_queue)) == nullptr) {
            report_queue_error("Malformed request", EPROTO);
            continue;
        }
        RequestTrace trace;
        trace.record("received request");
        const auto response = process_request(request, trace);
        // No resource locks are held while waiting for console output.
        trace.record(response.text);
        trace.print(worker_id, request);
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

bool parse_options(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (index + 1 >= argc) return false;
        const std::string value(argv[++index]);
        if (argument == "--workers") {
            if (!parse_integer(value, options.workers) || options.workers <= 0)
                return false;
        } else if (argument == "--sync") {
            if (!parse_bool_value(value, options.synchronize)) return false;
        } else if (argument == "--delay") {
            if (!parse_bool_value(value, options.delay)) return false;
        } else if (argument == "--verbose") {
            if (!parse_bool_value(value, options.verbose)) return false;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (!parse_options(argc, argv)) {
        std::cerr << "Usage: " << argv[0]
                  << " [--workers N] [--sync on|off] [--delay on|off]"
                     " [--verbose on|off]\n";
        return 1;
    }

    MessageQueue request_queue;
    if (!request_queue.create(REQUEST_QUEUE_NAME, O_RDONLY, QUEUE_MAX_MESSAGES,
                              sizeof(RequestMessage), 0666)) {
        std::cerr << "Cannot create request queue: " << std::strerror(errno)
                  << ". Check whether another server is running or a stale queue remains.\n";
        return 1;
    }
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::cout << "Server started: workers=" << options.workers
              << ", sync=" << on_off(options.synchronize)
              << ", random_delay=" << on_off(options.delay)
              << ", verbose=" << on_off(options.verbose) << '\n'
              << "Request queue: " << REQUEST_QUEUE_NAME << std::endl;

    std::vector<std::thread> workers;
    try {
        workers.reserve(static_cast<std::size_t>(options.workers));
        for (int id = 0; id < options.workers; ++id)
            workers.emplace_back(worker_loop, id + 1, request_queue.descriptor());
    } catch (const std::exception& error) {
        stop_requested.store(true);
        for (auto& worker : workers) worker.join();
        std::cerr << "Cannot start workers: " << error.what() << '\n';
        return 1;
    }
    for (auto& worker : workers) worker.join();
    request_queue.close();
    std::cout << "Server stopped and request queue removed\n";
    return 0;
}
