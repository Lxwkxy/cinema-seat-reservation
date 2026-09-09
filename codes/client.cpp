#include "common.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <mqueue.h>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

using namespace osproj;

bool parse_line(const std::string& line, Command& command, int& resource_id) {
    std::istringstream input(line);
    std::string token;
    if (!(input >> token) || !parse_command(token, command)) {
        return false;
    }

    resource_id = -1;
    if (command_requires_resource(command)) {
        if (!(input >> resource_id)) {
            return false;
        }
    }

    std::string extra_token;
    return !(input >> extra_token);
}

std::string make_response_queue_name(int client_id) {
    std::ostringstream output;
    output << "/osproj_client_" << static_cast<long long>(getpid())
           << "_" << client_id;
    return output.str();
}

bool send_request(mqd_t request_queue, mqd_t response_queue,
                  const std::string& response_queue_name, int client_id,
                  Command command, int resource_id) {
    RequestMessage request = {};
    request.command = static_cast<int>(command);
    request.client_id = client_id;
    request.resource_id = resource_id;
    copy_text(request.reply_queue, sizeof(request.reply_queue),
              response_queue_name);

    const timespec send_deadline =
        deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    if (mq_timedsend(request_queue,
                     reinterpret_cast<const char*>(&request),
                     sizeof(request), 0, &send_deadline) == -1) {
        std::cerr << "mq_send failed: " << std::strerror(errno) << '\n';
        return false;
    }

    ResponseMessage response = {};
    const timespec receive_deadline =
        deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
    const ssize_t received = mq_timedreceive(
        response_queue,
        reinterpret_cast<char*>(&response),
        sizeof(response), 0, &receive_deadline);

    if (received == -1) {
        std::cerr << "mq_receive failed: " << std::strerror(errno) << '\n';
        return false;
    }

    if (response.success) {
        std::cout << "SUCCESS: ";
    } else {
        std::cout << "FAILED: ";
    }
    std::cout << response.text << '\n';

    return true;
}

bool run_once(int argc, char** argv, int command_start, mqd_t request_queue,
              mqd_t response_queue, const std::string& response_queue_name,
              int client_id) {
    if (command_start >= argc) {
        std::cerr << "Missing command after --once\n";
        return false;
    }

    Command command;
    if (!parse_command(argv[command_start], command)) {
        std::cerr << "Unknown command\n";
        return false;
    }

    int resource_id = -1;
    if (command_requires_resource(command)) {
        if (command_start + 1 >= argc) {
            std::cerr << "Missing resource id\n";
            return false;
        }
        if (!parse_integer(argv[command_start + 1], resource_id)) {
            std::cerr << "Invalid resource id\n";
            return false;
        }
    }

    return send_request(request_queue, response_queue, response_queue_name,
                        client_id, command, resource_id);
}

}  // namespace

int main(int argc, char** argv) {
    int client_id = static_cast<int>(getpid() % 100000);
    bool once = false;
    int command_start = -1;
    int exit_code = 0;

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--id" && index + 1 < argc) {
            if (!parse_integer(argv[++index], client_id)) {
                std::cerr << "Invalid client id\n";
                return 1;
            }
        } else if (argument == "--once") {
            once = true;
            command_start = index + 1;
            break;
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--id N] [--once COMMAND [RESOURCE_ID]]"
                      << '\n';
            return 1;
        }
    }

    const std::string response_queue_name =
        make_response_queue_name(client_id);

    struct mq_attr response_attributes = {};
    response_attributes.mq_maxmsg = RESPONSE_QUEUE_MAX_MESSAGES;
    response_attributes.mq_msgsize = sizeof(ResponseMessage);

    mq_unlink(response_queue_name.c_str());
    mqd_t response_queue = mq_open(
        response_queue_name.c_str(),
        O_CREAT | O_RDONLY,
        0600,
        &response_attributes);

    if (response_queue == static_cast<mqd_t>(-1)) {
        std::cerr << "Cannot create response queue: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    mqd_t request_queue = mq_open(REQUEST_QUEUE_NAME, O_WRONLY);
    if (request_queue == static_cast<mqd_t>(-1)) {
        std::cerr << "Cannot open request queue. Is the server running? "
                  << std::strerror(errno) << '\n';
        mq_close(response_queue);
        mq_unlink(response_queue_name.c_str());
        return 1;
    }

    if (once) {
        if (!run_once(argc, argv, command_start, request_queue, response_queue,
                      response_queue_name, client_id)) {
            exit_code = 1;
        }
    } else {
        std::cout << "Client-" << client_id
                  << " ready. Commands: LIST, STATUS n, RESERVE n, "
                     "CANCEL n, QUIT"
                  << '\n';

        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            Command command;
            int resource_id;
            if (!parse_line(line, command, resource_id)) {
                std::cout << "Invalid command\n";
                continue;
            }

            if (!send_request(request_queue, response_queue,
                              response_queue_name, client_id, command,
                              resource_id) || command == Command::QUIT) {
                break;
            }
        }
    }

    mq_close(request_queue);
    mq_close(response_queue);
    mq_unlink(response_queue_name.c_str());
    return exit_code;
}
