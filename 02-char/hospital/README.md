# Hospital Patient Card

A patient registration desk: age, name and blood type are each validated, then a unique 4-digit ID is generated and stored.

## What it does

- Three stations in a row — age, name, blood type — each with its own attempt limit.
- A station that runs out of attempts stops the whole program: no half-saved patient.
- A unique ID between 1000 and 9999, checked against every ID already stored.
- IDs are kept in a fixed array with a counter, and the counter is the next free slot.
- `while (true)` wraps everything, so one run registers many patients.

## The struct

```cpp
struct Patient
{
    uint16_t id;
    char name[30];
    char Blood_type[4];
    uint8_t age;
};
```

`sizeof(Patient)` is 38, not 37: the compiler adds padding so `id` starts on an even address. Measured with `offsetof`.

`char Blood_type[4]` holds `AB+` plus the terminating `'\0'`.

## Edge cases tested

| Input | Result |
|---|---|
| age `25` | accepted |
| age `abc` | rejected, attempt counts |
| age `1abc` | rejected: `cin.peek()` catches the trailing junk |
| age `0`, `200` | out of range |
| age wrong 6 times | program stops |
| name `Ali` | accepted |
| name `Ali5` | rejected: digits are not allowed |
| name empty | rejected |
| name longer than 29 | rejected |
| blood `B+`, `AB-`, `O+` | accepted |
| blood `C+`, `A2B`, `ABC` | rejected |
| two patients in one run | both stored, both listed at the end |
| array full | refused before asking anything |
| same ID generated twice | regenerated, up to 5 tries |

## Lessons learned

- `break` only leaves the nearest loop. Nested stations meant a correct blood type sent the program back to asking for the name, then the age. Separate stations in sequence removed the trap entirely.
- A flag is an exit stamp: set it only at the real moment of success, and read it *after* the loop, since a loop can be left two ways — by `break`, or by running out of attempts.
- `rand()` with no `srand()` starts from seed 1 every run, so the first ID was always 1041. `srand(time(0))` is called once, at the top of `main`.
- `srand()` inside the generator would reseed per call, and two patients in the same second would share an ID.
- Check for a free slot *before* asking for any data, not after. Otherwise a patient fills in everything only to be told the hospital is full.
- Guard with `>=`, never `>`: with `MAX` slots the last index is `MAX - 1`, and `>` writes one past the end.
- A retry loop needs a hard limit. `while (true)` until a free ID appears would hang forever once all 9000 IDs are taken.
- `count` is already a name in the standard library; the counter here is `saved`.

## Build

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main
```
