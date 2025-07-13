// An interactive command line program that allows a user to:
// 1. Select a window by title (substring match)
// 2. Search for an int32 repeatedly.
// 3. When there is only one valid address, monitors it by indefinitely printing it out.
#define STRICT
#define NOMINMAX
#include <Windows.h>
#include <Winuser.h>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

#include "memory_scanner.hpp"
#include "memory_scanner_exception.hpp"

namespace
{

std::string GetUserInput(const std::string_view prompt)
{
	std::cout << prompt << std::flush;
	std::string user_response;
	if (!std::getline(std::cin, user_response)) {
		throw memory_scanner::MemoryScannerException("End of input");
	}
	return user_response;
}

bool GetUserYesNo(const std::string_view prompt)
{
	for (;;) {
		const std::string response = GetUserInput(prompt);
		if (response == "Y" || response == "y") {
			return true;
		} else if (response == "N" || response == "n") {
			return false;
		} else {
			std::cout << "Please answer Y or N" << std::endl;
		}
	}
}

int32_t GetUserInt32(const std::string_view prompt)
{
	const std::string response = GetUserInput(prompt);
	int32_t number = 0;
	std::from_chars(response.data(), response.data() + response.size(), number);
	return number;
}

struct EnumWindowsInfo {
	std::string_view compare_against;
	HWND last_hwnd;
	int num_matches;
	int num_checked;
};

BOOL CALLBACK enum_windows_func(_In_ HWND hwnd, _In_ LPARAM lParam)
{
	char buf[128];
	const int len = GetWindowTextA(hwnd, buf, sizeof(buf));
	const std::string_view title(buf, len);
	EnumWindowsInfo &enum_windows_info = *reinterpret_cast<EnumWindowsInfo *>(lParam);
	++enum_windows_info.num_checked;
	size_t pos = title.find(enum_windows_info.compare_against);
	if (pos != title.npos) {
		if (pos == 0 && title.size() == enum_windows_info.compare_against.size()) {
			std::cout << "  Exact match: [" << title << "]" << std::endl;
		} else {
			std::cout << "  Partial match: [" << title << "]" << std::endl;
		}
		++enum_windows_info.num_matches;
		enum_windows_info.last_hwnd = hwnd;
	}
	return TRUE;
}

// Locates an hwnd by window title, matching based on case-sensitive substring.
HWND FindWindowFuzzy(const std::string_view search_string)
{
	if (search_string.empty()) {
		std::cout << "Empty search string!" << std::endl;
		return nullptr;
	}
	auto enum_windows_info = EnumWindowsInfo{
		.compare_against = search_string,
		.last_hwnd = nullptr,
		.num_matches = 0,
		.num_checked = 0,
	};
	if (!EnumWindows(enum_windows_func, reinterpret_cast<LPARAM>(&enum_windows_info))) {
		const DWORD ec = GetLastError();
		throw memory_scanner::MemoryScannerException("Could not enumerate windows", ec);
	}
	std::cout << "Checked " << enum_windows_info.num_checked << " windows total" << std::endl;
	if (enum_windows_info.num_matches == 1) {
		return enum_windows_info.last_hwnd;
	}
	if (enum_windows_info.num_matches == 0) {
		std::cout << "No matches! Remember this is case-sensitive!" << std::endl;
	} else {
		std::cout << "Too many matches! " << enum_windows_info.num_matches << std::endl;
	}
	return nullptr;
}

HANDLE GetProcessFromHwnd(const HWND hwnd)
{
	DWORD pid = 0;
	const DWORD thread_id = GetWindowThreadProcessId(hwnd, &pid);
	if (thread_id == 0) {
		const DWORD ec = GetLastError();
		throw memory_scanner::MemoryScannerException("Cannot get process id from window", ec);
	}
	HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (process == nullptr) {
		const DWORD ec = GetLastError();
		throw memory_scanner::MemoryScannerException("Could not get process handle", ec);
	}
	return process;
}

// An unrestricted scan, the first one.
std::vector<memory_scanner::IntPtr> DoNextScan(const HANDLE process, std::vector<memory_scanner::MemoryRegion> &regions)
{
	const int32_t user_value = GetUserInt32("Enter value to search: ");
	std::cout << "Searching " << user_value << "..." << std::endl;
	std::vector<memory_scanner::IntPtr> valid_addresses = memory_scanner::NextScan<int32_t>(process, regions,
		[user_value](const int32_t & /*prev*/, const int32_t &current) -> bool { return current == user_value; });
	std::cout << valid_addresses.size() << " valid addresses" << std::endl;
	return valid_addresses;
}

// A restricted scan, must be part of "valid addresses".
void DoNextScan(const HANDLE process, std::vector<memory_scanner::MemoryRegion> &regions,
	std::vector<memory_scanner::IntPtr> &valid_addresses)
{
	const int32_t user_value = GetUserInt32("Enter value to search: ");
	std::cout << "Searching " << user_value << "..." << std::endl;
	memory_scanner::NextScan<int32_t>(process, regions, valid_addresses,
		[user_value](const int32_t & /*prev*/, const int32_t &current) -> bool { return current == user_value; });
	std::cout << valid_addresses.size() << " valid addresses" << std::endl;
}

template<typename T>
void ContinuouslyReadMemoryAddress(HANDLE process, memory_scanner::IntPtr address)
{
	memory_scanner::MemoryObject<T> object;
	object.address = address;
	object.ReRead(process);
	T last_value = object.value;
	std::cout << last_value << std::endl;
	for (;;) {
		Sleep(100);
		object.ReRead(process);
		if (object.value != last_value) {
			std::cout << object.value << std::endl;
			last_value = object.value;
		}
	}
}

void Run()
{
	// Alternatively if you know the exact name of your window then you can just simply use FindWindow:
	//   const HWND hwnd = FindWindowA(nullptr, "Untitled - Notepad");
	HWND hwnd = nullptr;
	for (;;) {
		const std::string user_search_string = GetUserInput("Enter window name: ");
		hwnd = FindWindowFuzzy(user_search_string);
		if (hwnd != nullptr) {
			break;
		}
		std::cout << "Try again" << std::endl;
	}
	const HANDLE process = GetProcessFromHwnd(hwnd);
	for (;;) {
		std::vector<memory_scanner::MemoryRegion> regions = memory_scanner::InitialScan(process);
		{
			// Just for stats purposes.
			SIZE_T total_bytes_read = 0;
			for (const auto &region : regions) {
				total_bytes_read += region.length;
			}
			std::cout << "Total bytes read: " << total_bytes_read << ", ";
			std::cout << (total_bytes_read >> 20) << " MiB" << std::endl;
			std::cout << regions.size() << " memory regions" << std::endl;
		}
		// int32_t is hard coded here but could be any type.
		std::vector<memory_scanner::IntPtr> valid_addresses = DoNextScan(process, regions);
		for (;;) {
			if (valid_addresses.size() == 0) {
				if (GetUserYesNo("No valid addresses! Would you like to try again? (Y/N): ")) {
					break;
				} else {
					return;
				}
			}
			if (valid_addresses.size() == 1) {
				std::cout << "Only one valid address, reading value" << std::endl;
				ContinuouslyReadMemoryAddress<int32_t>(process, valid_addresses[0]);
				break;
			}
			DoNextScan(process, regions, valid_addresses);
		}
	}
}

}  // namespace

int main()
{
	static_assert(sizeof(void *) == 8, "You need to compile in 64 bit mode");

	try {
		Run();
	} catch (memory_scanner::MemoryScannerException &e) {
		std::cout << "\nFATAL" << std::endl;
		std::cout << e.message << std::endl;
	}

	return 0;
}
