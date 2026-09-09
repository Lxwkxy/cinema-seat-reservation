#ifndef COMMON_HPP
#define COMMON_HPP

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>

static const char REQUEST_QUEUE_NAME[] = "/osproj_requests";
// 10 is accepted by the default Linux mqueue limit on most installations.
static const long QUEUE_MAX_MESSAGES = 10;
static const long RESPONSE_QUEUE_MAX_MESSAGES = 10;
static const int RESOURCE_COUNT = 20;
static const int MAX_QUEUE_NAME = 64;
static const int MAX_TEXT = 1024;
static const int REQUEST_TIMEOUT_SECONDS = 5;
static const int MIN_RANDOM_DELAY_MS = 50;
static const int MAX_RANDOM_DELAY_MS = 500;
static const int LOAD_CLIENT_ID_OFFSET = 10000;

enum class Command : int {
    LIST = 1,
    STATUS = 2,
    RESERVE = 3,
    CANCEL = 4,
    QUIT = 5
};

struct RequestMessage {
    int command;
    int client_id;
    int resource_id;
    char reply_queue[MAX_QUEUE_NAME];
};

struct ResponseMessage {
    int command;
    int client_id;
    int success;
    int resource_id;
    int owner_id;
    char text[MAX_TEXT];
};

struct ResourceRecord {
    int id;
    // owner_id == -1 means that the resource is available.
    int owner_id;

    bool is_reserved() const {
        return owner_id != -1;
    }

    void reserve_for(int client_id) {
        owner_id = client_id;
    }

    void release() {
        owner_id = -1;
    }
};

inline timespec deadline_after_seconds(int seconds) {
    timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    return deadline;
}

inline bool parse_integer(const std::string& token, int& value) {
    if (token.empty()) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (errno == ERANGE || end == token.c_str() || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

inline const char* command_name(Command command) {
    switch (command) {
        case Command::LIST: return "LIST";
        case Command::STATUS: return "STATUS";
        case Command::RESERVE: return "RESERVE";
        case Command::CANCEL: return "CANCEL";
        case Command::QUIT: return "QUIT";
    }
    return "UNKNOWN";
}

inline bool command_requires_resource(Command command) {
    return command == Command::STATUS ||
           command == Command::RESERVE ||
           command == Command::CANCEL;
}

inline bool parse_command(const std::string& token, Command& command) {
    if (token == "LIST") {
        command = Command::LIST;
    } else if (token == "STATUS") {
        command = Command::STATUS;
    } else if (token == "RESERVE") {
        command = Command::RESERVE;
    } else if (token == "CANCEL") {
        command = Command::CANCEL;
    } else if (token == "QUIT") {
        command = Command::QUIT;
    } else {
        return false;
    }
    return true;
}

inline void copy_text(char* destination, std::size_t capacity,
                      const std::string& value) {
    if (capacity == 0) {
        return;
    }
    std::strncpy(destination, value.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
}

inline bool valid_resource_id(int resource_id) {
    return resource_id >= 1 && resource_id <= RESOURCE_COUNT;
}

#endif
