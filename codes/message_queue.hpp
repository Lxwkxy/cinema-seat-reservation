#ifndef MESSAGE_QUEUE_HPP
#define MESSAGE_QUEUE_HPP

#include "common.hpp"

#include <fcntl.h>
#include <mqueue.h>
#include <unistd.h>

// Only the object that creates a queue owns its name and may unlink it.
class MessageQueue {
public:
    MessageQueue() = default;
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;
    ~MessageQueue() { close(); }

    bool open(const std::string& name, int flags) {
        close();
        descriptor_ = mq_open(name.c_str(), flags);
        return descriptor_ != invalid_descriptor();
    }

    bool create(const std::string& name, int flags, long capacity,
                long message_size, mode_t permissions = 0600) {
        close();
        owned_name_ = name;
        mq_attr attributes = {};
        attributes.mq_maxmsg = capacity;
        attributes.mq_msgsize = message_size;
        descriptor_ = mq_open(name.c_str(), flags | O_CREAT | O_EXCL,
                              permissions, &attributes);
        if (descriptor_ == invalid_descriptor()) {
            owned_name_.clear();
            return false;
        }
        return true;
    }

    void close() {
        if (descriptor_ != invalid_descriptor()) {
            mq_close(descriptor_);
            descriptor_ = invalid_descriptor();
        }
        if (!owned_name_.empty()) {
            mq_unlink(owned_name_.c_str());
            owned_name_.clear();
        }
    }

    mqd_t descriptor() const { return descriptor_; }

private:
    static mqd_t invalid_descriptor() { return static_cast<mqd_t>(-1); }
    mqd_t descriptor_ = invalid_descriptor();
    std::string owned_name_;
};

enum class RequestOutcome { Succeeded, Rejected, Timeout, TransportError };

struct RequestResult {
    RequestOutcome outcome = RequestOutcome::TransportError;
    ResponseMessage response = {};
    int error_number = 0;

    bool received_response() const {
        return outcome == RequestOutcome::Succeeded ||
               outcome == RequestOutcome::Rejected;
    }
};

class ClientConnection {
public:
    bool open(int client_id, const std::string& prefix) {
        ready_ = false;
        request_queue_.close();
        response_queue_.close();
        client_id_ = client_id;
        response_name_ = prefix + std::to_string(static_cast<long long>(getpid()))
                         + "_" + std::to_string(client_id);
        if (response_name_.size() >= MAX_QUEUE_NAME) {
            errno = ENAMETOOLONG;
            return false;
        }
        if (!response_queue_.create(response_name_, O_RDONLY,
                                    RESPONSE_QUEUE_MAX_MESSAGES,
                                    sizeof(ResponseMessage))) return false;
        if (!request_queue_.open(REQUEST_QUEUE_NAME, O_WRONLY)) {
            const int error = errno;
            response_queue_.close();
            errno = error;
            return false;
        }
        ready_ = true;
        return true;
    }

    RequestResult send(Command command, int resource_id) {
        if (!ready_) return fail(ENOTCONN);
        RequestMessage request = {};
        request.command = static_cast<int>(command);
        request.client_id = client_id_;
        request.resource_id = resource_id;
        copy_text(request.reply_queue, sizeof(request.reply_queue), response_name_);

        // One deadline bounds the complete round trip, including EINTR retries.
        const auto deadline = deadline_after_seconds(REQUEST_TIMEOUT_SECONDS);
        int sent;
        do {
            sent = mq_timedsend(request_queue_.descriptor(),
                                reinterpret_cast<const char*>(&request),
                                sizeof(request), 0, &deadline);
        } while (sent == -1 && errno == EINTR);
        if (sent == -1) return fail(errno);

        RequestResult result;
        ssize_t received;
        do {
            received = mq_timedreceive(response_queue_.descriptor(),
                                       reinterpret_cast<char*>(&result.response),
                                       sizeof(result.response), nullptr, &deadline);
        } while (received == -1 && errno == EINTR);
        if (received == -1) return fail(errno);
        const auto& response = result.response;
        if (received != static_cast<ssize_t>(sizeof(response)) ||
            response.command != request.command || response.client_id != client_id_ ||
            (response.success != 0 && response.success != 1) ||
            std::memchr(response.text, '\0', sizeof(response.text)) == nullptr)
            return fail(EPROTO);

        result.outcome = RequestOutcome::Rejected;
        if (response.success) result.outcome = RequestOutcome::Succeeded;
        return result;
    }

private:
    RequestResult fail(int error) {
        // A late reply must never be consumed by a subsequent request.
        ready_ = false;
        RequestResult result;
        result.error_number = error;
        if (error == ETIMEDOUT) result.outcome = RequestOutcome::Timeout;
        return result;
    }

    MessageQueue request_queue_;
    MessageQueue response_queue_;
    std::string response_name_;
    int client_id_ = 0;
    bool ready_ = false;
};

#endif
