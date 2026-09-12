# Concurrent Reservation System

ระบบจองทรัพยากรแบบ Concurrent ด้วย C++11 และ POSIX Message Queue

## โครงสร้าง

- codes/server.cpp: Server และ Worker Threads
- codes/client.cpp: Interactive Client
- codes/client_load.cpp: Load/Stress Test Client
- codes/common.hpp: Message Struct, Commands และ Constants
- scripts/: คำสั่งช่วยรัน Server และ Load Test
- logs/: Server Log
- results/: ผลการทดลอง

## ความต้องการ

- Docker Desktop
- Docker Compose
- Linux Container ที่มี g++ และ POSIX Message Queue
- C++11

## Build ด้วย Docker

สร้าง Image:

    docker build -t os-reservation .

เปิด Container:

    docker run --name osproj --ipc=host -it --rm \
      -v "$PWD:/workspace" \
      -w /workspace os-reservation

ถ้าใช้ PowerShell ให้ใช้ path ของโฟลเดอร์ปัจจุบันแทนค่า $PWD

จากนั้น Compile ภายใน Container:

    mkdir -p bin logs results

    g++ -std=c++11 -O2 -Wall -Wextra -pthread -Icodes \
      codes/server.cpp -lrt -o bin/server

    g++ -std=c++11 -O2 -Wall -Wextra -pthread -Icodes \
      codes/client.cpp -lrt -o bin/client

    g++ -std=c++11 -O2 -Wall -Wextra -pthread -Icodes \
      codes/client_load.cpp -lrt -o bin/client_load

## ใช้ Docker Compose

    docker compose up -d
    docker exec -it osproj bash

จากนั้น Compile ด้วยคำสั่งเดียวกับด้านบน

## Run Server

เปิด Terminal แรก:

    ./bin/server --workers 3 --sync on --delay on

ตัวเลือก:

- --workers N: จำนวน Worker Threads
- --sync on|off: เปิดหรือปิด Mutex
- --delay on|off: เปิดหรือปิด random delay 50-500 ms

สำหรับ Sequential Baseline:

    ./bin/server --workers 1 --sync on --delay off

สำหรับ Race Condition Experiment:

    ./bin/server --workers 3 --sync off --delay on

สำหรับ Synchronized Experiment:

    ./bin/server --workers 3 --sync on --delay on

## Run Client

เปิด Terminal เพิ่มเติมด้วยคำสั่ง:

    docker exec -it osproj bash

จากนั้นรัน:

    ./bin/client --id 1
    ./bin/client --id 2
    ./bin/client --id 3
    ./bin/client --id 4
    ./bin/client --id 5

คำสั่งที่รองรับ:

    LIST
    STATUS 10
    RESERVE 10
    CANCEL 10
    QUIT

โหมดส่งคำสั่งครั้งเดียว:

    ./bin/client --id 1 --once STATUS 10
    ./bin/client --id 1 --once RESERVE 10

## Load Test

ใช้ Load Client:

    ./bin/client_load \
      --clients 50 \
      --requests 20 \
      --command STATUS \
      --resource 10

หรือใช้ Script:

    bash scripts/load_test.sh

Script จะเริ่มที่ 5 Logical Clients และเพิ่มทีละ 5 จนกว่า Client จะพบ Failure/Timeout
จากนั้นบันทึกผลไว้ใน results/ โดยไม่ต้อง Compile ใหม่เมื่อแก้เฉพาะ Script

สามารถกำหนดขอบเขตการทดสอบได้ด้วย Environment Variable:

    MAX_CLIENTS=1000 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh

ค่าเริ่มต้นของ MAX_CLIENTS เป็น 0 หมายถึงทดสอบต่อไปจนกว่าจะเกิด Failure/Timeout

Metrics ที่แสดง:

- Total Requests
- Success/Failure
- Timeout
- Elapsed Time
- Throughput
- Average Latency

การเพิ่ม Client จนระบบเริ่ม Timeout หรือ Queue เต็มถือเป็น Stress/Capacity Test ควรบันทึกจุดอิ่มตัวและสาเหตุที่เกิดขึ้น

## Cleanup

หยุด Server ด้วย Ctrl+C เพื่อให้ Server ลบ Request Queue:

    docker compose down

ถ้า Client หรือ Queue ค้าง ให้หยุด Container แล้วเริ่มใหม่:

    docker rm -f osproj

## การทดลองที่ต้องแสดง

1. Sequential Baseline: Worker 1 ตัว
2. Concurrent Without Synchronization: Worker 3 ตัว, ปิด Mutex, เปิด Delay
3. Concurrent With Synchronization: Worker 3 ตัว, เปิด Mutex, เปิด Delay

ใน Experiment 2 คาดหวังให้เห็นโอกาสเกิด Race Condition เมื่อ Client หลายตัวจอง Resource เดียวกัน ส่วน Experiment 3 ต้องมีผู้จองสำเร็จเพียงหนึ่ง Client