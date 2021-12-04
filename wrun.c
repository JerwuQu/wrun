#include <windows.h>
#include <shlwapi.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define VEC_IMPL
#define VEC_INIT_SIZE 1024

#define VEC_TYPE char
#include "vec.h"

#define u16 uint16_t
#define u32 uint32_t
#define u32_MAX 0xffffffff

#define WASSERT(result) do { \
		if (!(result)) { \
			fprintf(stderr, "Windows error %ld on line %d\n", GetLastError(), __LINE__); \
			exit(1); \
		} \
	} while (0);

void err(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

#define VEC_NAME str
#define VEC_TYPE char*
#include "vec.h"

typedef enum {
	WRENT_START,
	WRENT_PATH,
	WRENT_CUSTOM,
} index_source_t;

typedef struct {
	u16 titleLen, pathLen;
	u32 title; // char* in arena
	u32 path; // wchar_t* in arena
	index_source_t source;
} entry_t;

#define VEC_NAME entry
#define VEC_TYPE entry_t
#include "vec.h"

struct {
	vec_entry_t entries;
	vec_char_t arena;
} gIndex;

u32 arenaAppend(const void *data, u32 sz)
{
	const u32 osz = gIndex.arena.count;
	vec_char_push_arr(&gIndex.arena, data, sz);
	return osz;
}

void indexAppend(const wchar_t *path, index_source_t source)
{
	wchar_t pathName[MAX_PATH];
	memcpy(pathName, path, sizeof(pathName));
	PathStripPathW(pathName);

	char title[MAX_PATH];
	const int titleLen = WideCharToMultiByte(CP_UTF8, 0, pathName, -1, title, sizeof(title), 0, 0);
	WASSERT(titleLen);

	const u32 pathLen = wcslen(path);
	vec_entry_push(&gIndex.entries, (entry_t) {
		.source = source,
		.pathLen = pathLen,
		.path = arenaAppend(path, pathLen * sizeof(wchar_t)),
		.titleLen = titleLen,
		.title = arenaAppend(title, titleLen),
	});
}

bool isExecutable(const wchar_t *path)
{
	static const wchar_t INDEX_EXTS[][4] = {
		L"exe",
		L"lnk",
		L"bat",
		L"cmd",
		L"com",
	};
	const size_t pathLen = wcslen(path);
	for (size_t i = 0; i < sizeof(INDEX_EXTS) / sizeof(INDEX_EXTS[0]); i++) {
		if (!StrCmpIW(path + pathLen - 3, INDEX_EXTS[i])) {
			return true;
		}
	}
	return false;
}

void indexDir(const wchar_t *path, size_t maxDepth, index_source_t source, size_t _curDepth)
{
	if (_curDepth > maxDepth || path[0] == L'\\') return;
	WIN32_FIND_DATAW ffd;
	wchar_t findDir[MAX_PATH], joined[MAX_PATH];
	PathCombineW(findDir, path, L"*");
	HANDLE fh = FindFirstFileW(findDir, &ffd);
	if (fh == INVALID_HANDLE_VALUE) return;

	do {
		PathCombineW(joined, path, ffd.cFileName);
		if (ffd.cFileName[0] == '.') continue;
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			indexDir(joined, maxDepth, source, _curDepth + 1);
		} else if (isExecutable(joined)) {
			indexAppend(joined, source);
		}
	} while (FindNextFileW(fh, &ffd));

	FindClose(fh);
}

void indexStartMenu()
{
	const wchar_t *startMenuSuffix = L"Microsoft\\Windows\\Start Menu\\Programs";
	wchar_t tmp[MAX_PATH], dir[MAX_PATH];

	GetEnvironmentVariableW(L"AppData", tmp, sizeof(tmp));
	PathCombineW(dir, tmp, startMenuSuffix);
	indexDir(dir, 3, WRENT_START, 0);

	GetEnvironmentVariableW(L"ProgramData", tmp, sizeof(tmp));
	PathCombineW(dir, tmp, startMenuSuffix);
	indexDir(dir, 3, WRENT_START, 0);
}

void indexEnvPath()
{
	wchar_t pathEnv[MAX_PATH * 256];
	GetEnvironmentVariableW(L"PATH", pathEnv, sizeof(pathEnv));
	wchar_t *ptr = pathEnv, *endPtr;
	while ((endPtr = StrStrW(ptr, L";")) > ptr) {
		endPtr[0] = 0; // nullterm
		indexDir(ptr, 0, WRENT_PATH, 0);
		ptr = endPtr + 1;
	}
}

void indexCustomPath(const char *path)
{
	wchar_t wpath[MAX_PATH];
	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH)) {
		err("custom index path too long");
	}
	indexDir(wpath, 0, WRENT_CUSTOM, 0);
}

int _index_compare(const void *a, const void *b)
{
	const entry_t *ea = a, *eb = b;
	if (ea->source == eb->source) {
		return strncmp(
				&gIndex.arena.data[ea->title],
				&gIndex.arena.data[eb->title],
				min(ea->titleLen, eb->titleLen));
	} else {
		return ea->source - eb->source;
	}
}

void organizeIndex()
{
	qsort(gIndex.entries.data, gIndex.entries.count, sizeof(entry_t), _index_compare);
}

void showMenu(char *menuCmd)
{
	SECURITY_ATTRIBUTES sa = {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.bInheritHandle = TRUE,
	};
	HANDLE stdinR, stdinW, stdoutR, stdoutW;
	CreatePipe(&stdinR, &stdinW, &sa, 0);
	CreatePipe(&stdoutR, &stdoutW, &sa, 0);
	SetHandleInformation(stdinW, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(stdoutR, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION pi;
	STARTUPINFO si = {
		.cb = sizeof(STARTUPINFO),
		.hStdInput = stdinR,
		.hStdOutput = stdoutW,
		.hStdError = stdoutW,
		.dwFlags = STARTF_USESTDHANDLES,
	};
	WASSERT(CreateProcessA(NULL, menuCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi));
	CloseHandle(stdinR);
	CloseHandle(stdoutW);

	for (u32 i = 0; i < gIndex.entries.count; i++) {
		char str[MAX_PATH + 128];
		const entry_t *entry = &gIndex.entries.data[i];
		u32 strLen = entry->titleLen;
		memcpy(str, &gIndex.arena.data[entry->title], strLen);
		str[strLen++] = '\n';
		WriteFile(stdinW, str, strLen, NULL, NULL);
	}
	CloseHandle(stdinW);

	char out[32];
	const DWORD readMax = sizeof(out) - 1;
	DWORD totRead = 0, lastRead;
	while (totRead < readMax && ReadFile(stdoutR, &out[totRead], readMax - totRead, &lastRead, NULL)) {
		totRead += lastRead;
	}
	out[totRead] = 0; // nullterm

	CloseHandle(stdoutR);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	int choice;
	if (!StrToIntExA(out, STIF_SUPPORT_HEX, &choice) || choice < 0) {
		err("no choice");
	} else if ((u32)choice >= gIndex.entries.count) {
		err("choice out of range");
	}

	entry_t *chosen = &gIndex.entries.data[choice];
	wchar_t path[MAX_PATH];
	memcpy(path, &gIndex.arena.data[chosen->path], chosen->pathLen * sizeof(wchar_t));
	path[chosen->pathLen] = 0; // nullterm
	ShellExecuteW(0, L"open", L"explorer", path, 0, SW_SHOW);
}

void usage()
{
	// TODO: daemonize flag: run in background and accept window message to show menu
	// TODO: flag to use recency/history for better menu ordering
	// TODO: flag to enable sub-actions (aside from launch), stuff like "Open Directory" and "Remove from History"
	fprintf(stderr, "wrun <OPTIONS>\n"
		"USAGE:\n"
		"\t-menu <menu cmd>  menu command to invoke (required)\n"
		"\t-Nstart           don't index start menu\n"
		"\t-Npath            don't index PATH\n"
		"\t+index <path>     index a custom path\n"
	);
	exit(1);
}

int main(int argc, char **argv)
{
	char *menuCmd = NULL;
	bool doIndexStartMenu = true, doIndexEnvPath = true;
	vec_str_t customPaths = {0};

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-Nstart")) {
			doIndexStartMenu = false;
		} else if (!strcmp(argv[i], "-Npath")) {
			doIndexEnvPath = false;
		} else if (i + 1 == argc) {
			usage();
		} else if (!strcmp(argv[i], "-menu")) {
			menuCmd = argv[++i];
		} else if (!strcmp(argv[i], "+index")) {
			vec_str_push(&customPaths, argv[++i]);
		} else {
			usage();
		}
	}
	if (!menuCmd) {
		usage();
	}

	if (doIndexStartMenu) indexStartMenu();
	if (doIndexEnvPath) indexEnvPath();
	for (size_t i = 0; i < customPaths.count; i++) {
		indexCustomPath(customPaths.data[i]);
	}
	organizeIndex();
	showMenu(menuCmd);

	return 0;
}
