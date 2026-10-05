# C++ → AIoT Cyber

Learning C++ from the ground up as an **Internet of Things** student, on the way to **embedded security** for cars, aircraft and smart devices: AI + IoT + Cybersecurity.

Every stage ends with a small, real project.

## Why embedded-first

Microcontrollers have kilobytes of memory, not gigabytes. Measured on my machine:

| Program | Size |
|---|---|
| `hello` with `<iostream>` (static, `-Os`) | ~1 MB |
| `hello` with `printf` (static, `-Os`) | ~18 KB |
| `sizeof(std::string)` | 32 bytes |
| `sizeof(char[5])` | 5 bytes |

Automotive and safety standards (MISRA C++, AUTOSAR C++14) ban or tightly restrict heap allocation, exceptions and heavy libraries on microcontrollers. So this path goes deep on the embedded subset of C++, and only tours the full standard library to know its cost.

## Roadmap

| # | Topic | Project | Status |
|---|---|---|---|
| 00 | First programs | Circle and triangle area | ✅ |
| 01 | `iostream` and input validation | [PIN lock with an attempt limit](01-iostream/pin-lock) | ✅ |
| 02 | C strings: `char[]`, `'\0'`, buffer overflow | PIN lock v2, embedded style, no heap · [Hospital patient card](02-char/hospital) | 🔨 |
| 03 | Fixed-width types and bit manipulation | Register simulator: LEDs through bit masks | ⏳ |
| 04 | Functions, references, `const`, `constexpr` | Compile-time sensor conversions | ⏳ |
| 05 | Arrays, `std::array`, ring buffer | Sensor ring buffer without heap | ⏳ |
| 06 | Classes without heap, RAII | `Sensor` class with fixed storage | ⏳ |
| 07 | Memory: stack, heap, static allocation | Heap fragmentation experiment | ⏳ |
| 08 | `volatile` and memory-mapped I/O | Fake GPIO register | ⏳ |
| 09 | State machines | Smart door lock | ⏳ |
| 10 | Templates without heap | Fixed-capacity container | ⏳ |
| 11 | Protocols: UART frames, CRC, fuzzing | Frame parser and its fuzzer | ⏳ |
| 12 | Final project | Secure IoT node simulator, then real hardware (STM32 / ESP32) | ⏳ |

Side station: a quick tour of `std::string`, `std::vector` and `std::map` for embedded Linux, measuring what they cost.

## Build

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main
```

## Principles

- Validate every input, both its type and its meaning.
- Fail safely: limit attempts and lock after abuse.
- Know what every byte costs: no hidden heap, no hidden size.
- Keep code small, readable and warning-free.
