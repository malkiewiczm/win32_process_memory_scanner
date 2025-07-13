#include "memory_scanner.hpp"

#define STRICT
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <source_location>
#include <string>
#include <utility>

#include "memory_scanner_exception.hpp"

namespace memory_scanner
{

SIZE_T ReadRegionData(HANDLE process, MemoryRegion &memory_region)
{
	memory_region.data = std::make_unique<char[]>(memory_region.length);
	SIZE_T bytes_read = 0;
	void *address = reinterpret_cast<void *>(memory_region.base_address);
	if (!ReadProcessMemory(process, address, memory_region.data.get(), memory_region.length, &bytes_read)) {
		const DWORD ec = GetLastError();
		if (ec != ERROR_PARTIAL_COPY) {
			throw MemoryScannerException("Cannot read process memory", ec, address);
		}
	}
	return bytes_read;
}

std::vector<MemoryRegion> InitialScan(HANDLE process)
{
	std::vector<MemoryRegion> regions;
	for (char *address = nullptr;;) {
		MEMORY_BASIC_INFORMATION mem_info;
		const SIZE_T size = VirtualQueryEx(process, address, &mem_info, sizeof(mem_info));
		if (size == 0) {
			const DWORD ec = GetLastError();
			if (ec == ERROR_INVALID_PARAMETER) {
				break;
			}
			throw MemoryScannerException("Cannot VirtualQueryEx process", ec);
			continue;
		}
		address += mem_info.RegionSize;
		if (mem_info.State != MEM_COMMIT) {
			continue;
		}
		if (mem_info.Protect != PAGE_READWRITE && mem_info.Protect != PAGE_EXECUTE_READWRITE) {
			continue;
		}
		const char *const start = reinterpret_cast<const char *>(mem_info.BaseAddress);
		const char *const end = start + mem_info.RegionSize;
		MemoryRegion region;
		region.base_address = reinterpret_cast<IntPtr>(mem_info.BaseAddress);
		region.length = static_cast<IntPtr>(mem_info.RegionSize);
		const SIZE_T bytes_read = ReadRegionData(process, region);
		if (bytes_read != mem_info.RegionSize) {
			throw MemoryScannerException("Bytes read differs from region size");
		}
		regions.push_back(std::move(region));
	}
	return regions;
}

}  // namespace memory_scanner
