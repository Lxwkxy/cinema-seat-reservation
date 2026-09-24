# Cinema Seat Reservation System

ระบบจองที่นั่งโรงภาพยนตร์แบบ Concurrent ด้วย C++11 และ POSIX Message Queue

## สิ่งที่ต้องมี

- Git
- Docker Desktop ที่เปิดใช้งาน Linux Containers
- Docker Compose

ก่อนเริ่ม ให้เปิด Docker Desktop และรอจน Docker Engine พร้อมใช้งาน
โปรเจกต์จะติดตั้ง g++, Make และ POSIX Message Queue ภายใน Docker Container ให้เอง

## Quick Start

### 1. Clone Repository

ทำคำสั่งบน PowerShell:

~~~powershell
git clone https://github.com/Lxwkxy/cinema-seat-reservation.git
cd cinema-seat-reservation
~~~

### 2. เปิด Docker Container

ทำคำสั่งบน PowerShell ที่โฟลเดอร์โปรเจกต์ แล้วใช้หน้าต่างนี้เป็น Terminal หลัก:

~~~powershell
docker compose up -d --build
~~~

### 3. Build และตรวจโปรแกรม

รันจาก PowerShell; คำสั่งจะทำงานภายใน Container:

~~~powershell
docker compose exec cpp make
docker compose exec cpp make test
~~~

คำสั่ง `make` จะสร้างโปรแกรมต่อไปนี้ในโฟลเดอร์ `bin/`:

- bin/server
- bin/client
- bin/client_load

ถ้าต้องการ Build ใหม่ทั้งหมด:

~~~powershell
docker compose exec cpp make clean
docker compose exec cpp make
~~~

### 4. เปิด Server

เปิด Server ใน Terminal แรกจาก PowerShell ที่โฟลเดอร์โปรเจกต์:

~~~powershell
docker compose exec cpp ./bin/server --workers 3 --sync on --delay on --verbose on
~~~

Terminal นี้จะทำงานเป็น Server และต้องเปิดค้างไว้ขณะใช้ Client แบบ manual

Server สร้าง POSIX Request Queue ชื่อ `/osproj_requests` และ Compose ใช้ IPC ร่วมกัน
จึงเปิด Server ได้ครั้งละตัวเดียวสำหรับ Queue นี้ อย่าลบไฟล์ใน `/dev/mqueue` หรือหยุด
Process ที่ไม่แน่ใจว่าเป็นของรอบทดลองนี้

### 5. เปิด Client Terminal ใหม่

เปิด PowerShell ใหม่ที่โฟลเดอร์โปรเจกต์ แล้วรัน Client:

~~~powershell
docker compose exec cpp ./bin/client --id 1
~~~

เปิด Terminal ใหม่หนึ่งหน้าต่างต่อ Client แล้วรันคำสั่งหนึ่งบรรทัดต่อหน้าต่าง:

~~~powershell
docker compose exec cpp ./bin/client --id 2
~~~

Client ตัวถัดไปให้เปิด Terminal ใหม่อีกหน้าต่างและเปลี่ยน `--id 2` เป็น `--id 3`

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

~~~powershell
docker compose exec cpp ./bin/client --id 1 --once STATUS 10
docker compose exec cpp ./bin/client --id 1 --once RESERVE 10
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

เมื่อเปิด Server ใหม่ตาม Quick Start แล้ว เปิด PowerShell อีกหน้าต่าง:

~~~powershell
docker compose exec cpp bash scripts/demo_clients.sh
~~~

สคริปต์เปิด Client 5 ตัวเป็น Process แยกกัน และบันทึกผลใน `logs/demo-clients-*`:

| Client | คำสั่ง |
|---|---|
| 1 | `LIST` |
| 2 | `STATUS 1` |
| 3 | `RESERVE 1` |
| 4 | `CANCEL 2` |
| 5 | `QUIT` |

ก่อนส่งคำสั่ง สคริปต์ให้ Client-4 จอง Resource 2 เพื่อให้ `CANCEL 2` ทำงานได้
มันไม่เปิดหรือหยุด Server ให้ และอาจคืน exit code 1 หาก Client ล้มเหลว
หลัง Demo แล้ว Resource 1 ถูกจองโดย Client-3; เริ่ม Server ใหม่ก่อน Demo รอบถัดไป
ผล `LIST`/`STATUS` อาจต่างกันตามลำดับที่ Server ประมวลผล

ถ้าเขียน `logs/` ไม่ได้ ให้เปลี่ยนที่เก็บเป็น `/tmp`:

~~~powershell
docker compose exec cpp bash -lc "DEMO_LOG_ROOT=/tmp bash scripts/demo_clients.sh"
~~~

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

ก่อนเริ่ม ให้ Client-4 ส่ง `RESERVE 2` และรอ `SUCCESS`
จากนั้นเตรียมคำสั่งตามตารางและกด Enter ใน Client ทั้งห้าใกล้ ๆ กัน
`QUIT` ปิดเฉพาะ Client-5; Server ยังทำงานต่อ

### อ่าน Server Log

เปิด `--verbose on` เพื่อดูรายละเอียด Request:

| ค่า | รายละเอียด |
|---|---|
| ทุกบรรทัด | `seq`, เวลา, Worker ID, Client ID, Command และ Resource ID |
| `--sync on` | มี `entering/leaving critical section` ตอนถือ/ปล่อย Mutex |
| `--sync off` | ไม่มี Critical Section Log; ดู `check resource` และผลจองซ้ำแทน |
| `LIST` / `QUIT` | แสดง Resource เป็น `ALL` / `NONE` |

Log อาจพิมพ์ไม่เรียงตาม `seq` เพราะพิมพ์หลังปล่อย Mutex
ให้ใช้ `seq` เปรียบเทียบลำดับเหตุการณ์

## Load Test

### รายงานผลแบบอ่านง่าย (`report.txt`)

| การทดสอบ | ไฟล์หลักฐาน | เนื้อหา |
|---|---|---|
| Experiments 1–3 | `docs/experiment-evidence/<run-id>/...` | รายงานแต่ละ attempt, Server Log, Client output และสรุปผล |
| Load Test | `results/load-test-*/clients-<N>/` | `report.txt` และ Client output ใน `client.txt` แยกตามจำนวน Client |

Load report แยก Success, Rejected, Timeout, Transport Error, Setup Error และ Skipped
พร้อม throughput กับ average/P95/maximum latency; Setup Error และ Skipped ไม่ใช่การจองที่ถูกปฏิเสธ
Timeout หมายถึงไม่ทราบผลฝั่ง Server ไม่ได้ยืนยันว่าการจองล้มเหลว

### ขั้นตอนรัน Load Test แบบไม่กำหนดจำนวนสูงสุด

`load_test.sh` ไม่เปิดหรือหยุด Server ให้ ทำตามขั้นตอนนี้ตามลำดับ
คำสั่ง `docker compose exec cpp bash` จะเปิด Shell ใน Container และแสดง prompt
`root@<container>:/workspace#`:

~~~powershell
docker compose exec cpp bash
~~~

รันคำสั่งตรวจสอบจาก prompt `root@...:/workspace#`:

~~~bash
ps -C server -o pid,ppid,args || echo 'No server process visible'
ls -l /dev/mqueue/osproj_requests 2>/dev/null || echo 'Request Queue is absent'
~~~

อ่านผลก่อนเริ่ม:

- ถ้า `ps` แสดง Server ของคุณ ให้กด `Ctrl+C` ใน Terminal ที่เปิดมันไว้
- ถ้า Queue ยังอยู่ ให้ตรวจ Terminal/Container อื่น; อย่าลบ Queue หรือเดา PID
- เริ่ม Load Test เมื่อไม่พบทั้ง Server และ Queue เดิม

1. **Terminal 1 — PowerShell:** เข้า Container แล้วเปิด Server ค้างไว้

~~~powershell
docker compose exec cpp bash
~~~

จากนั้นรันใน Bash ภายใน Container:

~~~bash
./bin/server --workers 3 --sync on --delay off --verbose off
~~~

2. **Terminal 2 — PowerShell:** เข้า Container แล้วเพิ่มจำนวน Client ทีละ 5 จน Client คืน Exit Code ที่ไม่ใช่ 0

~~~powershell
docker compose exec cpp bash
~~~

จากนั้นรันใน Bash ภายใน Container:

~~~bash
MAX_CLIENTS=0 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

เริ่มจาก 5 Clients แล้วเพิ่มเป็น 10, 15, 20, ... โดยไม่กำหนดเพดาน
Script จะหยุดเมื่อ `client_load` คืน Exit Code ที่ไม่ใช่ 0 เช่น Timeout, Transport Error หรือ Setup Error
ดูสาเหตุใน `client.txt` และสถิติใน `report.txt` ของรอบนั้น:
`results/load-test-*/clients-<N>/`
เมื่อจบ กด `Ctrl+C` ใน Terminal 1 แล้วรอข้อความยืนยันว่า Server หยุดและ Queue ถูกนำออก

อย่ารันคำสั่ง Load Test ต่อท้าย `project_experiments.sh` โดยไม่เปิด Server ใหม่ก่อน:
Experiment Script เปิดและหยุด Server ของแต่ละรอบเอง

ถ้าจะสั่ง Load Client เอง (Server ต้องยังเปิดอยู่):

~~~powershell
docker compose exec cpp mkdir -p results
docker compose exec cpp ./bin/client_load --clients 5 --requests 1 --command RESERVE --resource 10 --experiment manual_reservation --report results/report.txt
~~~

คำสั่งนี้เขียนทับ `results/report.txt` ถ้ามีอยู่แล้ว เปลี่ยนชื่อไฟล์หากต้องเก็บผลเดิม
เปลี่ยนโฟลเดอร์ผลลัพธ์ของ Script ด้วย `EVIDENCE_ROOT` หรือ `RESULTS_ROOT`

ตัวอย่างส่ง `STATUS` 20 ครั้งจาก 50 Logical Clients:

~~~powershell
docker compose exec cpp ./bin/client_load --clients 50 --requests 20 --command STATUS --resource 10
~~~

ตัวเลือกหลัก: `--clients` จำนวน Client, `--requests` จำนวน Request ต่อ Client,
`--command` ใช้ `STATUS` หรือ `RESERVE`, `--resource` เลือก Resource 1–20
และ `--workload` ใช้ Resource เดียว (`same`) หรือวน Resource 1–20 (`round-robin`)

`MAX_CLIENTS=0` (ค่าเริ่มต้น) ไม่กำหนดเพดาน Client; Script เพิ่มจำนวนต่อไปจน `client_load`
คืน Exit Code ที่ไม่ใช่ 0 เช่น Timeout, Transport Error หรือ Setup Error; Server ต้องเปิดค้างไว้
ตลอดการทดสอบ
Latency นับทุก Request ที่เริ่มส่ง รวม Request ที่ล้มเหลว; Setup Error/Skipped ไม่มี latency

## Experiments

รันจาก PowerShell ที่โฟลเดอร์โปรเจกต์:

1. Terminal 1 เปิด Server; Terminal 2 ส่ง Load Client.
2. ก่อนเริ่ม Experiment 1 ให้หยุด Server จาก Quick Start.
3. ก่อนรอบถัดไป กด `Ctrl+C` แล้วรอ `Server stopped and request queue removed`.

Server ใหม่จะรีเซ็ตสถานะ Resource อย่าเปิด Server ซ้อนกัน เพราะใช้ Queue ชื่อเดียวกัน

### Experiment 1: Sequential Baseline

ใช้ 1 Worker, `sync on`, `delay off`; ให้ 5 Clients จอง Resource 10:

~~~powershell
docker compose exec cpp ./bin/server --workers 1 --sync on --delay off --verbose on
~~~

~~~powershell
docker compose exec cpp ./bin/client_load --clients 5 --requests 1 --command RESERVE --resource 10
~~~

คาดหวัง `success=1`, `rejected=4`, ไม่มี Timeout/Transport/Setup Error
Exit code `3` ปกติเมื่อมี Request ถูกปฏิเสธ

การทดลองนี้ใช้ Logical Clients ใน Process เดียว; Demo ด้านบนใช้ Client Processes แยกกัน

### Experiment 2: Concurrent Without Synchronization

ใช้ 3 Workers, `sync off`, `delay on`, `verbose on`; ให้ 20 Clients จอง Resource 10:

~~~powershell
docker compose exec cpp ./bin/server --workers 3 --sync off --delay on --verbose on
~~~

~~~powershell
docker compose exec cpp ./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10
~~~

อาจมีมากกว่า 1 Client จองสำเร็จ; ผลเปลี่ยนได้แต่ละรอบ หากไม่พบ Race ให้ลอง 50 หรือ 100 Clients

### Experiment 3: Concurrent With Synchronization

ใช้ 3 Workers, `sync on`, `delay on` และ Resource/Client ชุดเดิม:

~~~powershell
docker compose exec cpp ./bin/server --workers 3 --sync on --delay on --verbose on
~~~

~~~powershell
docker compose exec cpp ./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10
~~~

คาดหวัง `success=1`, `rejected=19`, ไม่มี Timeout; exit code `3` ปกติเมื่อมีการปฏิเสธ

### เก็บหลักฐาน Experiment 1–3 อัตโนมัติ

ปิด Server แบบ manual ก่อน แล้วใช้คำสั่งตรวจ Server/Queue ในหัวข้อ Load Test
เริ่ม Script ต่อเมื่อไม่พบทั้งสองอย่าง จาก PowerShell:

~~~powershell
docker compose exec cpp bash scripts/project_experiments.sh
~~~

Script จะเปิดและหยุด Server ของแต่ละรอบให้อัตโนมัติ แล้วบันทึกหลักฐานไว้ที่
`docs/experiment-evidence/<run-id>/`:

- Server Log, Client output, รายงานแต่ละ attempt, `summary.txt` และ `results.csv`
- Experiment 2 ลองได้สูงสุด 5 ครั้ง; เปลี่ยนจำนวน Client/ครั้งได้ด้วย `RACE_CLIENTS` และ `RACE_RETRIES`
- ถ้ามี Server/Queue เดิม Script จะหยุดโดยไม่แตะต้องของเดิม
- ถ้าจบด้วย Error ให้ตรวจหลักฐานที่สร้างไว้; อย่าแก้ผลหรือใช้ `RUN_ID` ซ้ำ

### Experiment 4: Load หรือ Capacity Test

ทำตามขั้นตอน **Load Test แบบไม่กำหนดจำนวนสูงสุด** ในหัวข้อ Load Test ด้านบน โดยเปิด Server
ใหม่หลังจบ Experiment Script และใช้ Terminal แยกสำหรับ Server กับ Load Test

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
