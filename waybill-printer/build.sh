#!/bin/sh
# Cross-compile the Windows program from Linux (needs mingw-w64).
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres res/app.rc -O coff -o app.res
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -s \
    main.cpp app.res -o WaybillPrinter.exe \
    -lcomctl32 -lcomdlg32 -lgdi32 -lmsimg32 -lwinspool -lshell32
rm -f app.res
ls -l WaybillPrinter.exe
