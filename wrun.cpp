#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <filesystem>

#include <cassert> // TODO remove
#include <cstring> // TODO remove
extern "C" { // TODO remove
#include <windows.h>
#include <shlwapi.h>
}

#define u16 uint16_t
#define u32 uint32_t

namespace fs = std::filesystem;

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

const std::set<std::string> INDEX_EXTS = { ".exe", ".lnk", ".bat", ".cmd", ".com", ".url" };

enum IndexSource {
	WRENT_START,
	WRENT_PATH,
	WRENT_CUSTOM,
};

struct Entry {
	size_t historyScore;
	std::u8string title;
	fs::path path; // lexically_normalized
	IndexSource source;
};

std::vector<Entry> gEntries;
std::map<fs::path, size_t> gHistory; // path is lexically_normalized

void indexAppend(const fs::path& path, IndexSource source)
{
	const u32 historyScore = gHistory.contains(path) ? gHistory[path] : 0;
	gEntries.push_back(Entry{
		.historyScore = historyScore,
		.title = path.filename().u8string(),
		.path = path.lexically_normal(),
		.source = source,
	});
}

bool isExecutable(const fs::path& path) {
	return INDEX_EXTS.contains(path.extension().string());
}

void indexDir(const fs::path& path, IndexSource source, bool recurse = false)
{
	try {
		if (recurse) {
			for (const auto& entry : fs::recursive_directory_iterator(path)) {
				if (isExecutable(entry.path())) {
					indexAppend(entry.path(), source);
				}
			}
		} else {
			for (const auto& entry : fs::directory_iterator(path)) {
				if (isExecutable(entry.path())) {
					indexAppend(entry.path(), source);
				}
			}
		}
	} catch (const std::exception& ex) {
		fprintf(stderr, "Failed to index: %s, %s\n", path.string().c_str(), ex.what());
	}
}

void indexStartMenu()
{
	indexDir(fs::path(std::getenv("AppData")).append("Microsoft/Windows/Start Menu/Programs"), WRENT_START, true);
	indexDir(fs::path(std::getenv("ProgramData")).append("Microsoft/Windows/Start Menu/Programs"), WRENT_START, true);
}

void indexEnvPath()
{
	const auto pathEnv = std::string(std::getenv("PATH"));
	size_t start = 0, end;
	while ((end = pathEnv.find(";", start)) != std::string::npos) {
		indexDir(pathEnv.substr(start, end - start), WRENT_PATH);
		start = end + 1;
	}
}

void indexCustomPath(const fs::path& path)
{
	indexDir(path, WRENT_CUSTOM);
}

void loadHistory(const fs::path& path)
{
	FILE *f = fopen(path.lexically_normal().string().c_str(), "r"); // TODO: use C++
	if (f) {
		u16 pathBinLen;
		u32 historyScore;
		while (fread(&historyScore, 4, 1, f) == 1) {
			assert(fread(&pathBinLen, 2, 1, f) == 1);
			assert(pathBinLen && pathBinLen < 10000);
			std::u8string path;
			path.resize(pathBinLen);
			assert(fread(path.data(), pathBinLen, 1, f) == 1);
			gHistory[fs::path(path).lexically_normal()] = historyScore;
		}
		fclose(f);
	}
}

void saveHistory(const fs::path& path)
{
	FILE *f = fopen(path.lexically_normal().string().c_str(), "w"); // TODO: use C++
	assert(f);
	for (auto& entry : gHistory) {
		// History is stored as: <launch count:u32><path byte len:u16><path bytes>
		// TODO: better format
		const u32 historyScore = (size_t)entry.second;
		assert(fwrite(&historyScore, 4, 1, f) == 1);
		const auto pathStr = entry.first.u8string();
		const u16 pathBinLen = pathStr.length();
		assert(fwrite(&pathBinLen, 2, 1, f) == 1);
		assert(fwrite(pathStr.c_str(), pathBinLen, 1, f) == 1);
	}
	fclose(f);
}

void organizeIndex()
{
	std::sort(gEntries.begin(), gEntries.end(), [](const Entry& a, const Entry& b) -> bool {
		if (a.historyScore == b.historyScore) {
			if (a.source == b.source) {
				return a.title < b.title;
			} else {
				return a.source < b.source;
			}
		} else {
			return a.historyScore > b.historyScore;
		}
	});
}

void showMenu(char *menuCmd)
{
	// TODO: switch to C++ methods
	// Create pipes for menu
	SECURITY_ATTRIBUTES sa = {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.bInheritHandle = TRUE,
	};
	HANDLE stdinR, stdinW, stdoutR, stdoutW;
	CreatePipe(&stdinR, &stdinW, &sa, 0);
	CreatePipe(&stdoutR, &stdoutW, &sa, 0);
	SetHandleInformation(stdinW, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(stdoutR, HANDLE_FLAG_INHERIT, 0);

	// Open menu
	PROCESS_INFORMATION pi;
	STARTUPINFO si = {
		.cb = sizeof(STARTUPINFO),
		.dwFlags = STARTF_USESTDHANDLES,
		.hStdInput = stdinR,
		.hStdOutput = stdoutW,
		.hStdError = stdoutW,
	};
	WASSERT(CreateProcessA(NULL, menuCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi));
	CloseHandle(stdinR);
	CloseHandle(stdoutW);

	// Send menu to stdin and close stdin
	for (Entry& entry : gEntries) {
		WriteFile(stdinW, entry.title.c_str(), entry.title.length(), NULL, NULL);
		WriteFile(stdinW, "\n", 1, NULL, NULL);
	}
	CloseHandle(stdinW);

	// Read output
	char out[32];
	const DWORD readMax = sizeof(out) - 1;
	DWORD totRead = 0, lastRead;
	while (totRead < readMax && ReadFile(stdoutR, &out[totRead], readMax - totRead, &lastRead, NULL)) {
		totRead += lastRead;
	}
	out[totRead] = 0; // nullterm

	// Clean up
	CloseHandle(stdoutR);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	// Validate choice
	int choice;
	if (!StrToIntExA(out, STIF_SUPPORT_HEX, &choice) || choice < 0) {
		err("no choice");
	} else if ((u32)choice >= gEntries.size()) {
		err("choice out of range");
	}
	const Entry& chosen = gEntries[choice];

	// Open choice
	ShellExecuteW(0, L"open", L"explorer", chosen.path.c_str(), 0, SW_SHOW);

	// Put in history
	gHistory[chosen.path.c_str()] = chosen.historyScore + 1;
}

void usage()
{
	// TODO: daemonize flag: run in background and accept window message to show menu
	// TODO: flag to enable sub-actions (aside from launch), stuff like "Open Directory" and "Remove from History"
	// TODO: flag to run indexing on another thread and save for next launch
	fprintf(stderr, "wrun <OPTIONS>\n"
		"USAGE:\n"
		"\t-menu <menu cmd>  menu command to invoke (required)\n"
		"\t-Nhistory         don't load or save history\n"
		"\t-Nstart           don't index start menu\n"
		"\t-Npath            don't index PATH\n"
		"\t+index <path>     index a custom path\n"
	);
	exit(1);
}

int main(int argc, char **argv)
{
	const auto historyPath = fs::path(std::getenv("APPDATA")).append("wrun_history.bin");

	char *menuCmd = NULL;
	bool useHistory = true, doIndexStartMenu = true, doIndexEnvPath = true;
	std::vector<std::string> customPaths;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-Nhistory")) {
			useHistory = false;
		} else if (!strcmp(argv[i], "-Nstart")) {
			doIndexStartMenu = false;
		} else if (!strcmp(argv[i], "-Npath")) {
			doIndexEnvPath = false;
		} else if (i + 1 == argc) {
			usage();
		} else if (!strcmp(argv[i], "-menu")) {
			menuCmd = argv[++i];
		} else if (!strcmp(argv[i], "+index")) {
			customPaths.push_back(argv[++i]);
		} else {
			usage();
		}
	}
	if (!menuCmd) {
		usage();
	}

	if (useHistory) {
		loadHistory(historyPath);
	}

	if (doIndexStartMenu) indexStartMenu();
	if (doIndexEnvPath) indexEnvPath();
	for (size_t i = 0; i < customPaths.size(); i++) {
		indexCustomPath(customPaths[i]);
	}
	organizeIndex();
	showMenu(menuCmd);

	if (useHistory) {
		saveHistory(historyPath);
	}

	return 0;
}
