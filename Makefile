wrun.exe: wrun.cpp
	x86_64-w64-mingw32-g++ -Wall -std=c++20 -s -O2 $^ -o $@ -static -lstdc++ -luser32 -lshlwapi

.PHONY: clean
clean:
	rm -f wrun.exe
