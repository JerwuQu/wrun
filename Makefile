wrun.exe: wrun.c
	$(CC) -Wall -Wextra -std=c99 -pedantic -s -O2 $^ -o $@ -static -luser32 -lshlwapi

.PHONY: clean
clean:
	rm -f wrun.exe
