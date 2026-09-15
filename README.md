# Cinema Seat Reservation System

ระบบจองที่นั่งโรงภาพยนตร์แบบ Concurrent ด้วย C++11 และ POSIX Message Queue

## สิ่งที่ต้องมี

- Git
- Docker Desktop ที่เปิดใช้งาน Linux Containers
- Docker Compose

โปรเจกต์จะติดตั้ง g++, Make และ POSIX Message Queue ภายใน Docker Container ให้เอง

## Quick Start

### 1. Clone Repository

ทำคำสั่งบน PowerShell:

~~~powershell
git clone https://github.com/Lxwkxy/cinema-seat-reservation.git
cd cinema-seat-reservation
~~~

### 2. เปิด Docker Container

ทำคำสั่งบน PowerShell ที่โฟลเดอร์โปรเจกต์:

~~~powershell
docker compose up -d --build
docker compose exec cpp bash
~~~

### 3. Build โปรแกรม

ทำคำสั่งภายใน Container:

~~~bash
make
~~~

คำสั่งนี้จะสร้างโปรแกรมต่อไปนี้ในโฟลเดอร์ bin/:

- bin/server
- bin/client
- bin/client_load

ถ้าต้องการ Build ใหม่ทั้งหมด:

~~~bash
make clean
make
~~~

### 4. เปิด Server

ใน Terminal ที่อยู่ภายใน Container:

~~~bash
./bin/server --workers 3 --sync on --delay on --verbose on
~~~

Terminal นี้จะทำงานเป็น Server และควรเปิดค้างไว้

### 5. เปิด Client Terminal ใหม่

เปิด PowerShell ใหม่ที่โฟลเดอร์ cinema-seat-reservation แล้วเข้า Container:

~~~powershell
docker compose exec cpp bash
~~~

จากนั้นจึงรัน Client ภายใน Container:

~~~bash
./bin/client --id 1
~~~

สามารถเปิด Client เพิ่มได้โดยใช้ Terminal ใหม่และเปลี่ยน Client ID:

~~~bash
./bin/client --id 2
./bin/client --id 3
~~~

## คำสั่ง Client

เมื่อเปิด Interactive Client แล้ว สามารถใช้คำสั่ง:

~~~text
LIST
STATUS 10
RESERVE 10
CANCEL 10
QUIT
~~~

หรือส่งคำสั่งครั้งเดียวโดยไม่เข้าโหมด Interactive:

~~~bash
./bin/client --id 1 --once STATUS 10
./bin/client --id 1 --once RESERVE 10
~~~

Resource ID ที่ใช้งานได้อยู่ระหว่าง 1 ถึง 20

## ภาพรวมการทำงาน

ระบบใช้ **POSIX Message Queue** ให้ Client หลายตัวส่งคำสั่งไปยัง Server
ที่มี Worker หลายตัวทำงานพร้อมกัน

```text
Client ── Request ──> /osproj_requests ──> Worker ── Response ──> Client
                                      │
                                      └── Shared Reservation Data
                                          + Resource Mutex
```

การทำงานมี 4 ขั้นตอน:

1. Client ส่ง Request ไปที่ `/osproj_requests`
2. Worker ที่ว่างรับ Request และประมวลผลคำสั่ง
3. Worker ส่ง Response กลับไปยัง Queue ของ Client
4. Resource Mutex ป้องกันการจอง Resource เดียวกันพร้อมกัน

Request ประกอบด้วย `command`, `client_id`, `resource_id` และชื่อ Response Queue
ส่วน Response ประกอบด้วยสถานะสำเร็จ, เจ้าของ Resource และข้อความผลลัพธ์

คำสั่งที่รองรับคือ `LIST`, `STATUS`, `RESERVE`, `CANCEL` และ `QUIT`
โดย Resource ID อยู่ระหว่าง 1–20

รายละเอียดการออกแบบระบบอยู่ที่ [docs/architecture.md](docs/architecture.md)

## Demo: 5 Clients ส่งคำสั่งต่างกัน

ใช้ Server จาก Quick Start โดยให้เปิดด้วย `--sync on` และ `--verbose on`
ถ้า Resource 1 หรือ 2 ถูกจองไปแล้ว ให้หยุด Server แล้วเริ่มใหม่ก่อน Demo

### Demo แบบ 6 Terminals สำหรับนำเสนอ

แต่ละ Client Terminal เข้า Container ด้วย `docker compose exec cpp bash` แล้วเปิดดังนี้:

| Terminal | โปรแกรม | คำสั่งที่จะส่งพร้อมกัน |
|---|---|---|
| 1 | Server ตามคำสั่งด้านบน | ดู Log |
| 2 | `./bin/client --id 1` | `LIST` |
| 3 | `./bin/client --id 2` | `STATUS 1` |
| 4 | `./bin/client --id 3` | `RESERVE 1` |
| 5 | `./bin/client --id 4` | `CANCEL 2` |
| 6 | `./bin/client --id 5` | `QUIT` |

ก่อนเริ่ม ให้ Terminal 5 ส่ง `RESERVE 2` และรอ SUCCESS แล้วเตรียมคำสั่งตามตาราง
กด Enter ในทั้ง 5 Client Terminals ในช่วงใกล้กัน หรือให้สมาชิกแต่ละคนควบคุมคนละ Terminal
ทุกคำสั่งควรสำเร็จ; LIST/STATUS อาจเห็นสถานะก่อนหรือหลังการแก้ไขตามลำดับประมวลผล
QUIT ปิดเฉพาะ Client-5 ส่วน Server ยังทำงานต่อ

### อ่าน Server Log

เปิด Log ด้วย `--verbose on` ทุกบรรทัดมี `seq`, เวลา, Worker ID, Client ID, Command
และ Resource ID โดย LIST แสดง `Resource-ALL` และ QUIT แสดง `Resource-NONE`
เมื่อเปิด sync จะมี `entering critical section` หลังได้ lock และ `leaving critical section`
ก่อนปล่อย lock รวมถึงคำสั่งที่ถูกปฏิเสธ; LIST บันทึกขอบเขตการล็อก snapshot ทั้งชุด
เมื่อปิด sync จะไม่มี Log เข้า/ออก Critical Section เพราะไม่ได้ถือ Resource Mutex

เหตุการณ์ถูกเก็บพร้อมเวลาและ sequence ณ จุดเกิดจริง แล้วพิมพ์รวมหลังปล่อย Resource Mutex
บรรทัดจากคนละ Request จึงอาจไม่เรียงตาม `seq`; ใช้ sequence เปรียบเทียบลำดับเหตุการณ์
Log ตรวจสถานะ `check resource: AVAILABLE/RESERVED` ช่วยอธิบายช่วง check/update

## Load Test

รัน Load Client ภายใน Container:

~~~bash
./bin/client_load --clients 50 --requests 20 --command STATUS --resource 10
~~~

ความหมายของตัวเลือก:

- --clients: จำนวน Logical Clients
- --requests: จำนวน Request ต่อ Client
- --command: STATUS หรือ RESERVE
- --resource: หมายเลข Resource

หรือใช้ Script ที่จะเพิ่มจำนวน Client ครั้งละ 5:

~~~bash
bash scripts/load_test.sh
~~~

กำหนดจำนวนสูงสุดของ Client ได้ด้วย:

~~~bash
MAX_CLIENTS=100 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

ถ้า MAX_CLIENTS เป็น 0 ระบบจะทดสอบต่อไปจนกว่าจะพบ Failure หรือ Timeout

Metrics ที่แสดง:

- Success: จำนวนคำสั่งที่สำเร็จ
- Rejected: จำนวนคำสั่งที่ถูกปฏิเสธ เช่น Resource ถูกจองแล้ว
- Timeout: จำนวนคำสั่งที่ใช้เวลานานเกินกำหนด
- Throughput: จำนวน Request ต่อวินาที
- Average Latency: เวลาเฉลี่ยต่อ Request

Script นี้ต้องรันภายใน Container และต้องใช้ Bash ไม่ใช่ sh

## Experiments

ก่อนเริ่ม Experiment ใหม่ ให้หยุด Server เดิมด้วย Ctrl+C แล้วเปิด Server ด้วยค่าของ Experiment ถัดไป

### Experiment 1: Sequential Baseline

วัดค่าพื้นฐานโดยใช้ Worker เพียง 1 ตัว:

~~~bash
./bin/server --workers 1 --sync on --delay off
~~~

เปิดอีก Terminal แล้วรัน:

~~~bash
./bin/client_load --clients 1 --requests 20 --command STATUS --resource 10
~~~

ผลที่ควรได้:

- Request สำเร็จทั้งหมด
- ไม่มี Timeout
- ใช้ Throughput และ Average Latency เป็น Baseline

### Experiment 2: Concurrent Without Synchronization

ทดสอบ Race Condition โดยปิด Mutex:

~~~bash
./bin/server --workers 3 --sync off --delay on --verbose on
~~~

เปิดอีก Terminal แล้วให้หลาย Client จอง Resource เดียวกัน:

~~~bash
./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10
~~~

ผลที่ควรสังเกต:

- อาจมีมากกว่า 1 Client ที่จองสำเร็จ
- ผลลัพธ์อาจแตกต่างกันในแต่ละรอบ
- หากยังไม่เห็น Race Condition ให้เพิ่มจำนวน Client เป็น 50 หรือ 100

### Experiment 3: Concurrent With Synchronization

ทดสอบการใช้ Mutex:

~~~bash
./bin/server --workers 3 --sync on --delay on --verbose on
~~~

ใช้คำสั่ง Client เดิม:

~~~bash
./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10
~~~

ผลที่ควรได้:

- มี Client จองสำเร็จเพียง 1 ตัว
- Client ที่เหลือได้รับผลว่า Resource ถูกจองแล้ว
- success=1, rejected=19 และ timeouts=0 หากระบบตอบทันเวลา (exit code 3)

### Experiment 4: Load หรือ Capacity Test

เปิด Server สำหรับวัด Load ใน Terminal หนึ่ง:

~~~bash
./bin/server --workers 3 --sync on --delay off
~~~

จากนั้นเปิดอีก Terminal แล้วรัน:

~~~bash
bash scripts/load_test.sh
~~~

กำหนดจำนวน Client สูงสุดได้ด้วย:

~~~bash
MAX_CLIENTS=100 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

หากต้องการทดสอบต่อไปจนกว่าจะพบ Failure หรือ Timeout:

~~~bash
MAX_CLIENTS=0 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

ให้สังเกตจำนวน Client ที่เริ่มเกิดปัญหา พร้อม Throughput, Average Latency และ Timeout

## สรุปการตั้งค่าแต่ละ Experiment

| Experiment | Workers | Sync | Delay | Workload | ผลที่คาดหวัง |
|---|---:|---|---|---|---|
| Sequential Baseline | 1 | On | Off | STATUS | ใช้เป็นค่าพื้นฐาน |
| Without Synchronization | 3 | Off | On | RESERVE Resource เดียวกัน | อาจเกิด Race Condition |
| With Synchronization | 3 | On | On | RESERVE Resource เดียวกัน | สำเร็จเพียง 1 Client |
| Load Test | 3 | On | Off | เพิ่มจำนวน Client | วัดจุดเริ่มต้นของ Failure หรือ Timeout |

## Project Structure

~~~text
.
├── codes/
│   ├── common.hpp
│   ├── message_queue.hpp
│   ├── server.cpp
│   ├── client.cpp
│   └── client_load.cpp
├── scripts/
│   ├── run_server.sh
│   ├── run_clients.sh
│   └── load_test.sh
├── docs/
│   └── architecture.md
├── Makefile
├── Dockerfile
├── docker-compose.yml
├── .dockerignore
├── .gitattributes
└── .gitignore
~~~

## Cleanup

หยุด Server ด้วย Ctrl+C แล้วออกจาก Container:

~~~bash
exit
~~~

จากนั้นทำคำสั่งบน PowerShell:

~~~powershell
docker compose down
~~~

ถ้า Container ค้าง ให้ใช้:

~~~powershell
docker compose down --remove-orphans
~~~
