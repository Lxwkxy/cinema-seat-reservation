# Cinema Seat Reservation System

ระบบจองที่นั่งโรงภาพยนตร์แบบ Concurrent ด้วย C++11 และ POSIX Message Queue

## โครงสร้าง

- codes/server.cpp: Server และ Worker Threads
- codes/client.cpp: Interactive Client
- codes/client_load.cpp: Load/Stress Test Client
- codes/common.hpp: Message Struct, Commands และ Constants
- Makefile: คำสั่ง Build โปรแกรมทั้งหมด
- scripts/: คำสั่งช่วยรัน Server และ Load Test
- logs/: Server Log

## ความต้องการ

- Docker Desktop
- Docker Compose
- Linux Container ที่มี g++ และ POSIX Message Queue
- Make
- C++11

## Build ด้วย Docker

สร้าง Image:

    docker build -t os-reservation .

เปิด Container:

    docker run --name osproj --ipc=host -it --rm -v "${PWD}:/workspace" -w /workspace os-reservation

คำสั่งนี้ใช้ได้ใน PowerShell เมื่อเปิด Docker Desktop แล้ว

จากนั้น Compile ภายใน Container:

    make

คำสั่ง `make` จะสร้างโปรแกรมไว้ใน `bin/` และจะ Compile ใหม่เฉพาะไฟล์ที่มีการเปลี่ยนแปลง

## ใช้ Docker Compose

    docker compose up -d
    docker exec -it osproj bash

จากนั้น Compile ด้วยคำสั่ง:

    make

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

Load Test ใช้ `client_load` เพื่อสร้าง Logical Clients หลายตัวและส่ง Request ไปยัง Server พร้อมกัน

ตัวอย่างการทดสอบด้วย Load Client:

    ./bin/client_load --clients 50 --requests 20 --command STATUS --resource 10

หรือใช้ Script ซึ่งจะแสดงผลการทดสอบบนหน้าจอ:

    bash scripts/load_test.sh

Script จะเริ่มที่ 5 Logical Clients และเพิ่มทีละ 5 จนกว่า Client จะพบ Failure หรือ Timeout

สามารถกำหนดขอบเขตการทดสอบได้ด้วย Environment Variable:

    MAX_CLIENTS=1000 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh

ความหมายของตัวแปร:

- `MAX_CLIENTS`: จำนวน Client สูงสุดที่ต้องการทดสอบ
- `STEP`: จำนวน Client ที่เพิ่มขึ้นในแต่ละรอบ
- `REQUESTS_PER_CLIENT`: จำนวน Request ที่แต่ละ Client ส่ง

ค่าเริ่มต้นของ `MAX_CLIENTS` เป็น `0` หมายถึงทดสอบต่อไปจนกว่าจะเกิด Failure หรือ Timeout

Metrics ที่แสดง:

- Total Requests
- Success
- Failure หรือ Timeout
- Elapsed Time
- Throughput
- Average Latency

การเพิ่ม Client จนระบบเริ่ม Timeout หรือ Queue เต็มถือเป็น Stress หรือ Capacity Test

## Demo Workflow

การทดลองทั้งหมดควรทำภายใน Docker Container เพื่อให้ใช้ C++11, POSIX Message Queue และ Linux Environment เดียวกัน

### เตรียมระบบก่อนเริ่ม Demo

ทำคำสั่งต่อไปนี้บน PowerShell ที่โฟลเดอร์หลักของโปรเจกต์:

    docker compose up -d

    docker exec -it osproj bash

ภายใน Container ให้ Compile โปรแกรม:

    make

สร้างโปรแกรมทั้งหมดด้วย `make` และลบไฟล์ Binary ด้วย:

    make clean

เมื่อแก้ `common.hpp` คำสั่ง `make` จะ Compile โปรแกรมที่ใช้ Header นี้ใหม่โดยอัตโนมัติ

เปิด Server ใน Terminal หนึ่ง โดยใช้คำสั่งจากแต่ละ Experiment ด้านล่าง

เปิด Client หรือ Load Test ใน Terminal ใหม่ด้วยคำสั่งบน PowerShell:

    docker exec -it osproj bash

จากนั้นจึงรันคำสั่ง Client ภายใน Container

### Experiment 1: Sequential Baseline

วัตถุประสงค์คือสร้างค่าพื้นฐานของระบบที่มี Worker เพียงตัวเดียว

ใน Terminal สำหรับ Server:

    ./bin/server --workers 1 --sync on --delay off

ใน Terminal สำหรับ Client:

    ./bin/client_load --clients 1 --requests 20 --command STATUS --resource 10

การตั้งค่านี้หมายถึง:

- ใช้ Worker 1 ตัว
- เปิด Synchronization
- ปิด Random Delay
- ใช้ Client 1 ตัว
- ส่งทั้งหมด 20 Requests

ผลที่ควรสังเกต:

- Request ควรสำเร็จทั้งหมด
- ไม่ควรเกิด Timeout
- ค่า Throughput และ Average Latency ใช้เป็น Baseline

### Experiment 2: Concurrent Without Synchronization

วัตถุประสงค์คือแสดง Race Condition เมื่อหลาย Worker เข้าถึง Resource เดียวกันโดยไม่มี Mutex

หยุด Server เดิมด้วย `Ctrl+C` แล้วเปิดใหม่ด้วยคำสั่ง:

    ./bin/server --workers 3 --sync off --delay on

ใน Terminal สำหรับ Client ให้ส่งคำสั่งจอง Resource เดียวกันพร้อมกัน:

    ./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10

การตั้งค่านี้หมายถึง:

- ใช้ Worker 3 ตัว
- ปิด Synchronization
- เปิด Random Delay 50-500 milliseconds
- ให้ Client 20 ตัวจอง Resource หมายเลข 10

ผลที่ควรสังเกต:

- อาจมีมากกว่า 1 Client ที่จองสำเร็จ
- ผลลัพธ์ขึ้นอยู่กับ Timing ของแต่ละรอบ
- Server Log อาจแสดงหลาย Worker ตรวจพบ Resource ว่างในช่วงเวลาใกล้กัน

หากยังไม่เห็น Race Condition ให้เริ่ม Server ใหม่และเพิ่มจำนวน Client:

    ./bin/client_load --clients 50 --requests 1 --command RESERVE --resource 10

หรือ:

    ./bin/client_load --clients 100 --requests 1 --command RESERVE --resource 10

### Experiment 3: Concurrent With Synchronization

วัตถุประสงค์คือเปรียบเทียบผลหลังจากเปิด Mutex เพื่อป้องกัน Critical Section

หยุด Server เดิมด้วย `Ctrl+C` แล้วเปิดใหม่ด้วยคำสั่ง:

    ./bin/server --workers 3 --sync on --delay on

ใช้ Workload เดียวกับ Experiment 2:

    ./bin/client_load --clients 20 --requests 1 --command RESERVE --resource 10

ผลที่ควรสังเกต:

- ควรมีผู้จองสำเร็จเพียง 1 Client
- Client ที่เหลือควรได้รับผลว่า Resource ถูกจองแล้ว
- ไม่ควรมีการจอง Resource เดียวกันสำเร็จซ้ำหลายครั้ง
- Server Log จะแสดงการเข้าและออกจาก Critical Section

ผลลัพธ์ที่คาดหวังโดยประมาณคือ:

    success=1
    failure_or_timeout=19

### Experiment 4: Load หรือ Capacity Test

วัตถุประสงค์คือดูว่าระบบรองรับจำนวน Client ได้มากแค่ไหนก่อนเริ่มเกิด Failure หรือ Timeout

เริ่ม Server ในรูปแบบที่เหมาะกับการวัด Throughput:

    ./bin/server --workers 3 --sync on --delay off

จากนั้นรัน Load Test แบบกำหนดขอบเขต:

    MAX_CLIENTS=100 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh

Script จะทดสอบตามลำดับ:

    5 Clients, 10 Clients, 15 Clients, ... จนถึง 100 Clients

หากต้องการเพิ่ม Client ต่อไปจนกว่าจะพบ Failure หรือ Timeout ให้ใช้:

    MAX_CLIENTS=0 STEP=5 REQUESTS_PER_CLIENT=20 bash scripts/load_test.sh

เมื่อระบบเริ่มมีปัญหา ให้สังเกต:

- จำนวน Client ในรอบที่เริ่ม Failure
- จำนวน Request ที่สำเร็จและล้มเหลว
- Elapsed Time
- Throughput
- Average Latency
- จำนวน Timeout

ควรใช้จุดที่ระบบเริ่ม Failure หรือ Timeout เป็น Saturation Point ของการทดลอง

### ตารางเปรียบเทียบการทดลอง

| Experiment | Workers | Sync | Delay | Workload | ผลที่คาดหวัง |
|---|---:|---|---|---|---|
| Sequential Baseline | 1 | On | Off | STATUS | ใช้เป็นค่าพื้นฐาน |
| Without Synchronization | 3 | Off | On | จอง Resource เดียวกัน | อาจเกิด Success มากกว่า 1 |
| With Synchronization | 3 | On | On | จอง Resource เดียวกัน | Success ต้องเท่ากับ 1 |
| Load Test | 3 | On | Off | เพิ่มจำนวน Client | วัดจุดเริ่มต้นของ Failure หรือ Timeout |

## Cleanup

หลังจบการทดลอง ให้หยุด Server ด้วย `Ctrl+C` เพื่อให้ Server ปิด Request Queue อย่างถูกต้อง

ออกจาก Container:

    exit

หยุด Docker Compose จาก PowerShell:

    docker compose down

ถ้า Client หรือ Queue ค้าง ให้หยุด Container แล้วเริ่มใหม่:

    docker rm -f osproj
