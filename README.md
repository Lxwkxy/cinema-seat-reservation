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

รายละเอียดสถาปัตยกรรมและฟิลด์ Request/Response อยู่ที่ [docs/architecture.md](docs/architecture.md)

## Demo: 5 Clients ส่งคำสั่งต่างกัน

ใช้ Server จาก Quick Start โดยให้เปิดด้วย `--sync on` และ `--verbose on`
ถ้า Resource 1 หรือ 2 ถูกจองไปแล้ว ให้หยุด Server แล้วเริ่มใหม่ก่อน Demo

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

### Experiment 4: Load หรือ Capacity Test

ใช้ Worker 3 ตัว เปิด `sync` ปิด `delay` และปิด `verbose` จากนั้นเพิ่มจำนวน Client เพื่อหาจุดที่ระบบเริ่มเกิดข้อผิดพลาด
การทดลองนี้ใช้คำสั่ง `STATUS 10` เพื่อวัดการรองรับคำขอและเวลาตอบสนอง

**Terminal 1 — เปิด Server:**

~~~powershell
docker compose exec cpp ./bin/server --workers 3 --sync on --delay off --verbose off
~~~

เปิดหน้าต่างนี้ค้างไว้ระหว่างทดสอบ การใช้ `--verbose off` ช่วยลดภาระจากการพิมพ์ Log รายคำขอ

**Terminal 2 — รัน Load Test:**

~~~powershell
docker compose exec cpp bash scripts/load_test.sh
~~~

สคริปต์เริ่มจาก **5 Clients** แล้วเพิ่มเป็น **10, 15, 20, ...** โดยแต่ละ Client ส่ง `STATUS 10` จำนวน **20 ครั้ง**
เช่น รอบที่มี 5 Clients จะมีคำขอที่วางแผนไว้ 100 ครั้ง และรอบที่มี 10 Clients จะมี 200 ครั้ง
สคริปต์เพิ่มจำนวนต่อไปจน `client_load` คืน exit code ที่ไม่ใช่ 0 เช่น พบ Timeout, Transport Error หรือ Setup Error
ตอนหยุดจะแสดง `Load test stopped at ... clients` พร้อมผลของรอบนั้น

ถ้าต้องการทดสอบไม่เกิน **50 Clients** ให้ใช้คำสั่งนี้แทน:

~~~powershell
docker compose exec -e MAX_CLIENTS=50 cpp bash scripts/load_test.sh
~~~

ผลแต่ละรอบจะแสดงบน Terminal 2 ทั้งจำนวนคำขอ, Success, Errors, Throughput และ Average/P95/Maximum Latency
ไฟล์ผลลัพธ์อยู่ที่ `results/load-test-*/clients-<N>/` โดยสคริปต์จะแสดงชื่อโฟลเดอร์ของรอบที่รัน:

- `report.txt`: รายงานอ่านง่าย พร้อมผลราย Client และสถิติรวม
- `client.txt`: ข้อความจากโปรแกรม รวมรายละเอียดข้อผิดพลาด

เปรียบเทียบรอบที่สำเร็จครบล่าสุดกับรอบแรกที่เริ่มเกิดข้อผิดพลาด เพื่อดูจำนวน Client ที่รองรับได้ในการทดลองครั้งนั้น
Setup Error นับ Client ที่เตรียมไม่สำเร็จ ส่วน Skipped นับคำขอที่ยังไม่ได้เริ่มส่ง
Latency รวมทุกคำขอที่เริ่มส่ง ทั้งสำเร็จและล้มเหลว; Setup Error และ Skipped ไม่มีตัวอย่าง Latency

เมื่อทดสอบเสร็จ ให้กด **Ctrl+C ใน Terminal 1** เพื่อจบการทำงานของ Server

### เก็บหลักฐาน Experiment 1–3 อัตโนมัติ

รันสคริปต์นี้เพื่อเก็บหลักฐาน Experiment 1–3 อัตโนมัติ:

~~~powershell
docker compose exec cpp bash scripts/project_experiments.sh
~~~

Script จะเปิดและหยุด Server ของแต่ละรอบให้อัตโนมัติ แล้วบันทึกหลักฐานไว้ที่
`docs/experiment-evidence/<run-id>/`:

- Server Log, Client output, รายงานแต่ละ attempt, `summary.txt` และ `results.csv`
- Experiment 2 ลองได้สูงสุด 5 ครั้ง; เปลี่ยนจำนวน Client/ครั้งได้ด้วย `RACE_CLIENTS` และ `RACE_RETRIES`
- ถ้ามี Server/Queue เดิม Script จะหยุดโดยไม่แตะต้องของเดิม
- ถ้าจบด้วย Error ให้ตรวจหลักฐานที่สร้างไว้; อย่าแก้ผลหรือใช้ `RUN_ID` ซ้ำ

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

กด **Ctrl+C** ในหน้าต่าง Server แล้วรันคำสั่งนี้จาก PowerShell:

~~~powershell
docker compose down
~~~

## การแก้ปัญหา

### ต้องการ Build ใหม่ทั้งหมด

ลบโปรแกรมที่ Build ไว้แล้วคอมไพล์ใหม่:

~~~powershell
docker compose exec cpp make clean
docker compose exec cpp make
~~~

### Demo เขียนโฟลเดอร์ logs ไม่ได้

เปลี่ยนที่เก็บ Log เป็น `/tmp` ภายใน Container:

~~~powershell
docker compose exec -e DEMO_LOG_ROOT=/tmp cpp bash scripts/demo_clients.sh
~~~

### เก็บกวาด Container ที่เหลือจากบริการเดิม

หากมี Container ของบริการที่ไม่มีอยู่ใน Compose ของโปรเจกต์แล้ว ให้ใช้:

~~~powershell
docker compose down --remove-orphans
~~~
