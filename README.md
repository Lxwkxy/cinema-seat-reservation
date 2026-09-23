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

รัน regression tests ของ Metrics, workload mapping และการ cleanup ของชุดทดลอง:

~~~bash
make test
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

### Demo แบบ Terminal เดียวสำหรับ Client ทั้ง 5 ตัว

เมื่อเปิด Server ใหม่ตาม Quick Start แล้ว รันใน Bash ภายใน Container อีก Terminal:

~~~bash
bash scripts/demo_clients.sh
~~~

สคริปต์เตรียมให้ Client-4 จอง Resource 2 ก่อน แล้วเปิด `bin/client --once`
เป็น 5 processes ด้วย `&` ส่ง LIST, STATUS 1, RESERVE 1, CANCEL 2 และ QUIT
โดยเปิดครบทั้ง 5 ก่อน `wait` รอผล เป็น Client processes จริงที่ใช้ Terminal แสดงผลร่วมกัน
เวลาที่เริ่มและเสร็จขึ้นกับ OS scheduler จึงไม่รับประกันว่าจะทำงานพร้อมกันทุกช่วงเวลา

แสดง PID และผลแยกตาม Client พร้อมเก็บ Log ในโฟลเดอร์ใหม่ `logs/demo-clients-*`
หากโฟลเดอร์นี้เขียนไม่ได้ เปลี่ยนที่เก็บด้วย `DEMO_LOG_ROOT=/tmp bash scripts/demo_clients.sh`
ถ้า Client ใดล้มเหลว สคริปต์คืน exit code 1 สคริปต์ไม่เปิดหรือหยุด Server ให้
หลังจบ Resource 1 จะเป็นของ Client-3 และ Resource 2 จะว่าง ให้เปิด Server ใหม่ก่อนรันซ้ำ
LIST/STATUS อาจเห็นสถานะก่อนหรือหลังการจองตามลำดับประมวลผล

วิธีนี้สาธิตหลาย Client จาก Terminal เดียว ส่วนการนำเสนอตามข้อกำหนดหลาย Terminal
ให้ใช้ขั้นตอนด้านล่าง

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

### รายงานผลแบบอ่านง่าย (`report.txt`)

`scripts/project_experiments.sh` สร้างรายงานแยกทุก Experiment และทุก attempt ที่
`docs/experiment-evidence/<run-id>/<experiment>_attempt_<n>/report.txt`
แสดง Target seat, ผลราย Client และยอดจองสำเร็จ/ถูกปฏิเสธ พร้อมการตั้งค่า Server
ใช้ชื่อ `client-1`, `client-2`, ... ซึ่งตรงกับ Server ID 10001, 10002, ...

`scripts/load_test.sh` สร้างโฟลเดอร์ใหม่ทุกครั้ง และแยกรายงานตามจำนวน Clients ที่
`results/load-test-*/clients-<N>/report.txt` พร้อม output เดิมใน `client.txt`
ตารางสรุปแต่ละ Client แสดง success/rejected/timeout/transport error/skipped
และ average latency ส่วนท้ายมี throughput, average/P95/maximum latency รวม
ค่า Setup Error และ Skipped ไม่ถูกนับเป็นการจองที่ถูกปฏิเสธ
Timeout ไม่ได้ยืนยันว่าฝั่ง Server จองล้มเหลว รายงานจึงระบุว่า server outcome unknown

รันจาก Bash ภายใน Container:

~~~bash
# หยุด Server เดิมก่อน: สคริปต์เปิด/ปิด Server สำหรับแต่ละ Experiment ให้
bash scripts/project_experiments.sh

# Load Test ต้องเปิด Server ไว้ก่อน เช่น --workers 3 --sync on --delay off
MAX_CLIENTS=20 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

สร้างรายงานโดยตรงได้ด้วย (โฟลเดอร์ปลายทางต้องมีอยู่ และไฟล์เดิมจะถูกเขียนทับ):

~~~bash
./bin/client_load --clients 5 --requests 1 --command RESERVE --resource 10 \
  --experiment manual_reservation --report results/report.txt
~~~

รายงานสร้างจากผล Client จริงหลังจบการรัน ไม่แปลงยอดรวมเก่าเป็นผลราย Client
ผลทดลองที่บันทึกไว้ก่อนเพิ่มความสามารถนี้จะยังไม่มี `report.txt` ต้องรันใหม่
หากโฟลเดอร์เขียนไม่ได้ เปลี่ยนด้วย `EVIDENCE_ROOT` สำหรับ Experiments
หรือ `RESULTS_ROOT` สำหรับ Load Test

รัน Load Client ภายใน Container:

~~~bash
./bin/client_load --clients 50 --requests 20 --command STATUS --resource 10
~~~

ความหมายของตัวเลือก:

- --clients: จำนวน Logical Clients
- --requests: จำนวน Request ต่อ Client
- --command: STATUS หรือ RESERVE
- --resource: หมายเลข Resource
- --workload: `same`/`same-resource` ใช้ Resource เดียว หรือ `round-robin` ให้ Client แต่ละตัวใช้ Resource ตามหมายเลข Client วน 1–20

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
- Transport Error และ Setup Error: แยกข้อผิดพลาดของการสื่อสารออกจากการเปิด Client/Queue
- Planned, Attempted และ Skipped Requests: Request ที่วางแผนไว้, เริ่มส่งแล้ว และถูกข้าม
- Throughput: จำนวน Request ต่อวินาที
- Average Latency, P95 และ Maximum Latency: คำนวณจาก Request ที่เริ่มส่งแล้ว

Latency ของ Request ที่เริ่มส่งแล้วรวมทั้ง Success, Rejected, Timeout และ Transport Error
ส่วน Setup Error และ Skipped Request ไม่มี latency sample. Throughput ใช้จำนวน Attempted
หารด้วย elapsed time ของ Load Test ทั้งชุด. P95 ใช้ latency sample รวมของทุก Logical Client
ใน trial เดียวด้วย nearest-rank method ไม่ได้นำ P95 ราย Client มาเฉลี่ย

Script นี้ต้องรันภายใน Container และต้องใช้ Bash ไม่ใช่ sh

## Experiments

ก่อนเริ่ม Experiment ใหม่ ให้หยุด Server เดิมด้วย Ctrl+C แล้วเปิด Server ด้วยค่าของ Experiment ถัดไป

### Experiment 1: Sequential Baseline

ตรวจสอบการประมวลผลแบบลำดับโดยใช้ Worker เพียง 1 ตัว และให้ Client หลายตัวจอง Resource เดียวกัน:

~~~bash
./bin/server --workers 1 --sync on --delay off --verbose on
~~~

เปิดอีก Terminal แล้วรัน Load Client ซึ่งสร้าง Logical Client 5 ตัวให้ส่ง Request ใกล้เคียงกัน:

~~~bash
./bin/client_load --clients 5 --requests 1 --command RESERVE --resource 10
~~~

ผลที่ควรได้:

- มี Request ทั้งหมด 5 รายการ และแต่ละ Client ส่ง `RESERVE 10` หนึ่งครั้ง
- `success=1`, `rejected=4`, `timeouts=0`, `transport_errors=0` และ `setup_errors=0`
- มี Client เดียวเป็นเจ้าของ Resource 10 เพราะ Worker ตัวเดียวประมวลผล Request ทีละรายการ
- Load Client อาจคืน exit code `3` เมื่อมี Request ถูกปฏิเสธ ซึ่งเป็นผลที่คาดไว้ในการทดลองนี้

Experiment นี้ใช้ Logical Clients ภายใน Load Client เพื่อทดสอบหลาย Request ผ่าน Request Queue เดียวกัน
การสาธิต Client ที่เป็น Process แยกกันจากหลาย Terminal ให้ทำตามหัวข้อ Demo หลาย Terminal ด้านบน
หากต้องการบันทึก Throughput/Latency เป็นค่าพื้นฐาน ให้ใช้ `STATUS` แยกอีกรอบ เพราะการจองมีผลให้ Request หลังจากผู้ชนะถูกปฏิเสธ

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

### เก็บหลักฐาน Experiment 1–3 อัตโนมัติ

ภายใน Docker หลัง Build โปรแกรมแล้ว รัน:

~~~bash
bash scripts/project_experiments.sh
~~~

Script จะเปิด Server ใหม่สำหรับแต่ละรอบ ใช้ Worker/Sync/Delay ตามการทดลองข้างต้น
และเก็บ Server Log, Client Metrics, คำสั่งและ Environment ไว้ใน
`docs/experiment-evidence/<run-id>/`. Experiment 2 จะลองซ้ำได้สูงสุด 5 ครั้งจนกว่าจะ
พบมากกว่าหนึ่ง Client จอง Resource 10 สำเร็จ; ทุก attempt จะถูกเก็บไว้ แม้ยังไม่พบ Race.
ปรับจำนวน Client หรือจำนวนครั้งที่ลองได้ด้วย `RACE_CLIENTS` และ `RACE_RETRIES`.
ถ้ามี Server หรือ Request Queue อยู่ก่อนแล้ว Script จะหยุดโดยไม่แตะต้องของเดิม.

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
| Sequential Baseline | 1 | On | Off | RESERVE Resource 10 จาก 5 Clients | สำเร็จ 1 Client และถูกปฏิเสธ 4 |
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
│   ├── client_load.cpp
│   ├── load_metrics.hpp
│   └── load_workload.hpp
├── scripts/
│   ├── experiment_helpers.sh
│   ├── project_experiments.sh
│   ├── run_server.sh
│   ├── demo_clients.sh
│   └── load_test.sh
├── tests/
│   ├── experiment_harness_test.sh
│   ├── client_load_metrics_test.cpp
│   └── client_load_workload_test.cpp
├── docs/
│   ├── architecture.md
│   └── experiment-evidence/<run-id>/
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
