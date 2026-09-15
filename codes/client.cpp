#include "message_queue.hpp"

#include <iostream>

struct ClientOptions {
    int client_id = static_cast<int>(getpid());
    bool once = false;
    ParsedCommand command;
};

bool parse_options(int argc, char** argv, ClientOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--id" && index + 1 < argc) {
            if (!parse_integer(argv[++index], options.client_id) ||
                options.client_id <= 0) return false;
        } else if (argument == "--once") {
            options.once = true;
            std::ostringstream line;
            while (++index < argc) line << argv[index] << ' ';
            return parse_command_line(line.str(), options.command);
        } else {
            return false;
        }
    }
    return true;
}

RequestResult execute(ClientConnection& connection, const ParsedCommand& command) {
    const auto result = connection.send(command.command, command.resource_id);
    if (!result.received_response()) {
        std::cerr << "Request failed: " << std::strerror(result.error_number) << '\n';
        return result;
    }
    if (result.outcome == RequestOutcome::Succeeded) std::cout << "SUCCESS: ";
    else std::cout << "FAILED: ";
    std::cout << result.response.text << '\n';
    return result;
}

int main(int argc, char** argv) {
    ClientOptions options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " [--id POSITIVE_ID] [--once COMMAND [RESOURCE_ID]]\n"
                  << "Commands: LIST, STATUS n, RESERVE n, CANCEL n, QUIT; n=1.."
                  << RESOURCE_COUNT << '\n';
        return 1;
    }

    ClientConnection connection;
    if (!connection.open(options.client_id, "/osproj_client_")) {
        std::cerr << "Cannot connect to server: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (options.once) {
        const auto result = execute(connection, options.command);
        if (!result.received_response()) return 1;
        if (result.outcome == RequestOutcome::Rejected) return 2;
        return 0;
    }

    std::cout << "Client-" << options.client_id
              << " ready. Commands: LIST, STATUS n, RESERVE n, CANCEL n, QUIT\n";
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        ParsedCommand command;
        if (!parse_command_line(line, command)) {
            std::cout << "Invalid command or resource ID\n";
            continue;
        }
        const auto result = execute(connection, command);
        if (!result.received_response()) return 1;
        if (command.command == Command::QUIT) break;
    }
    return 0;
}
