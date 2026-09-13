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
./bin/server --workers 3 --sync on --delay on
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

- Total Requests
- Success
- Failure หรือ Timeout
- Elapsed Time
- Throughput
- Average Latency

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
./bin/server --workers 3 --sync off --delay on
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
./bin/server --workers 3 --sync on --delay on
~~~

ใช้คำสั่ง Client เดิม:

~~~bash
./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10
~~~

ผลที่ควรได้:

- มี Client จองสำเร็จเพียง 1 ตัว
- Client ที่เหลือได้รับผลว่า Resource ถูกจองแล้ว
- โดยประมาณ success=1 และ failure_or_timeout=19

### Experiment 4: Load หรือ Capacity Test

เปิด Server สำหรับวัด Load:

~~~bash
./bin/server --workers 3 --sync on --delay off
~~~

จากนั้นรัน Load Test แบบต่อเนื่อง:

~~~bash
bash scripts/load_test.sh
~~~

หรือกำหนดจำนวน Client สูงสุด:

~~~bash
MAX_CLIENTS=100 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

ระบบจะทดสอบตั้งแต่ 5, 10, 15 ไปจนถึง 100 Clients

หากต้องการทดสอบต่อเนื่องจนพบ Failure หรือ Timeout:

~~~bash
MAX_CLIENTS=0 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh
~~~

ให้บันทึกค่ารอบที่เริ่มเกิดปัญหา พร้อม Throughput, Average Latency และจำนวน Failure หรือ Timeout

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
│   ├── server.cpp
│   ├── client.cpp
│   └── client_load.cpp
├── scripts/
│   ├── run_server.sh
│   ├── run_clients.sh
│   └── load_test.sh
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

ถ้า Container ค้าง:

~~~powershell
docker compose down --remove-orphans
~~~
