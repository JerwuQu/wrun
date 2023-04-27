wrun.exe: wrun.cpp
	x86_64-w64-mingw32-g++ -std=c++20 -pedantic -Wall -Wextra -Werror -s -O2 $^ -o $@ -static -lstdc++ -luser32 -lshlwapi

.PHONY: run-single run-daemon
run-single: wrun.exe
	./$< --menu 'wlines.exe -id'

run-daemon: wrun.exe
	./$< --daemonize --menu 'wlines.exe -id'

.PHONY: clean
clean:
	rm -f wrun.exe
