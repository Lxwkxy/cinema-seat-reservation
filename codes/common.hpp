#ifndef COMMON_HPP
#define COMMON_HPP

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sstream>

static const char REQUEST_QUEUE_NAME[] = "/osproj_requests";
// กำหนดให้รองรับข้อความใน Queue ได้สูงสุด 10 รายการตามข้อจำกัดทั่วไปของ Linux
static const long QUEUE_MAX_MESSAGES = 10;
static const long RESPONSE_QUEUE_MAX_MESSAGES = 1;
static const int RESOURCE_COUNT = 20;
static const int MAX_QUEUE_NAME = 64;
static const int MAX_TEXT = 1024;
static const int REQUEST_TIMEOUT_SECONDS = 5;
static const int MIN_RANDOM_DELAY_MS = 50;
static const int MAX_RANDOM_DELAY_MS = 500;
static const int LOAD_CLIENT_ID_OFFSET = 10000;
static const int NO_OWNER = -1;

// คำสั่งที่ Client ส่งให้ Server โดยใช้ค่าตัวเลขเดียวกันในข้อความ IPC
enum class Command : int {
    LIST = 1,
    STATUS = 2,
    RESERVE = 3,
    CANCEL = 4,
    QUIT = 5
};

// รูปแบบข้อมูล Request แบบ binary ที่ส่งผ่าน Request Message Queue
struct RequestMessage {
    int command;
    int client_id;
    int resource_id;
    char reply_queue[MAX_QUEUE_NAME];
};

// รูปแบบข้อมูล Response ที่ Server ส่งกลับไปยัง Queue ของ Client
struct ResponseMessage {
    int command;
    int client_id;
    int success;
    int resource_id;
    int owner_id;
    char text[MAX_TEXT];
};

// สถานะของ Resource หนึ่งรายการ รวมถึง Client ที่เป็นเจ้าของ
struct ResourceRecord {
    int id;
    // ถ้า owner_id == -1 หมายความว่า Resource ยังว่าง
    int owner_id;

    // ตรวจสอบว่า Resource ถูกจองอยู่หรือไม่
    bool is_reserved() const {
        return owner_id != NO_OWNER;
    }

    // กำหนดให้ Client เป็นเจ้าของ Resource
    void reserve_for(int client_id) {
        owner_id = client_id;
    }

    // ล้างเจ้าของ Resource เพื่อยกเลิกการจอง
    void release() {
        owner_id = NO_OWNER;
    }
};

// สร้างเวลาหมดอายุจากเวลาปัจจุบันตามจำนวนวินาทีที่กำหนด
inline timespec deadline_after_seconds(int seconds) {
    timespec deadline = {};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    return deadline;
}

// แปลงข้อความเป็นจำนวนเต็มและตรวจสอบค่าที่เกินขอบเขตของ int
inline bool parse_integer(const std::string& token, int& value) {
    if (token.empty()) return false;

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (errno == ERANGE || end == token.c_str() || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX) return false;

    value = static_cast<int>(parsed);
    return true;
}

// แปลงค่าคำสั่งจาก enum ให้เป็นข้อความสำหรับแสดงผล
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

// ตรวจสอบว่าคำสั่งนั้นต้องระบุ Resource ID หรือไม่
inline bool command_requires_resource(Command command) {
    return command == Command::STATUS ||
           command == Command::RESERVE ||
           command == Command::CANCEL;
}

// แปลงข้อความคำสั่งจากผู้ใช้ให้เป็นค่า Command
inline bool parse_command(const std::string& token, Command& command) {
    if (token == "LIST") command = Command::LIST;
    else if (token == "STATUS") command = Command::STATUS;
    else if (token == "RESERVE") command = Command::RESERVE;
    else if (token == "CANCEL") command = Command::CANCEL;
    else if (token == "QUIT") command = Command::QUIT;
    else return false;
    return true;
}

// คัดลอกข้อความลง Buffer พร้อมบังคับให้มี null terminator
inline void copy_text(char* destination, std::size_t capacity,
                      const std::string& value) {
    if (capacity == 0) return;
    std::strncpy(destination, value.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
}

// ตรวจสอบว่า Resource ID อยู่ในช่วง 1 ถึง RESOURCE_COUNT
inline bool valid_resource_id(int resource_id) {
    return resource_id >= 1 && resource_id <= RESOURCE_COUNT;
}

// คำสั่งที่แยกวิเคราะห์แล้ว พร้อม Resource ID เมื่อคำสั่งต้องใช้
struct ParsedCommand {
    Command command = Command::LIST;
    int resource_id = NO_OWNER;
};

// แยกวิเคราะห์คำสั่งทั้งโหมด Interactive และโหมด --once ด้วยรูปแบบเดียวกัน
inline bool parse_command_line(const std::string& line, ParsedCommand& result) {
    std::istringstream input(line);
    std::string token;
    ParsedCommand parsed;
    if (!(input >> token) || !parse_command(token, parsed.command)) return false;
    if (command_requires_resource(parsed.command)) {
        if (!(input >> token) || !parse_integer(token, parsed.resource_id) ||
            !valid_resource_id(parsed.resource_id)) return false;
    }
    if (input >> token) return false;
    result = parsed;
    return true;
}

// แปลงค่า Boolean ให้เป็นข้อความ on หรือ off
inline const char* on_off(bool enabled) {
    if (enabled) return "on";
    return "off";
}

#endif
