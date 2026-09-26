# C++ → AIoT Cyber

Learning C++ from the ground up as an **Internet of Things** student, on the way to **embedded security**: AI + IoT + Cybersecurity.

Every stage ends with a small, real project.

## Roadmap

| # | Topic | Project | Status |
|---|---|---|---|
| 00 | First programs | Circle and triangle area | ✅ |
| 01 | `iostream` and input validation | PIN lock with an attempt limit | 🔨 |
| 02 | `std::string` | Password strength checker | ⏳ |
| 03 | Functions and references | Calculator split into functions | ⏳ |
| 04 | `std::vector` | Temperature sensor logger | ⏳ |
| 05 | `struct` and `class` | `Sensor` class, IoT style | ⏳ |
| 06 | Memory: `new`/`delete`, `unique_ptr` | Why memory bugs happen, and how to prevent them | ⏳ |
| 07 | Files with `fstream` | Device event log | ⏳ |
| 08 | `std::map` and `<algorithm>` | Small user database | ⏳ |
| 09 | Templates | A tool that works with any type | ⏳ |
| 10 | Final project | IoT gateway simulator: sensors, log, secure login | ⏳ |

## Build

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main
```

## Principles

- Validate every input, both its type and its meaning.
- Fail safely: limit attempts and lock after abuse.
- Keep code small, readable and warning-free.
