#pragma once

#define STRICT
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory_scanner_exception.hpp"

namespace memory_scanner
{

using IntPtr = ULONG_PTR;

// Represents a continuous region in process memory that can span multiple pages.
class MemoryRegion
{
public:
	IntPtr base_address = 0;
	IntPtr length = 0;
	std::unique_ptr<char[]> data = nullptr;

	bool ContainsAddress(const IntPtr address) const
	{
		return address >= base_address && address < (base_address + length);
	}
};

// Represents a single value found at an address in process memory, for example like an int32 or double.
template<typename T>
class MemoryObject
{
public:
	IntPtr address;
	T value;

	// Reads the address dictated by `.address` and populates it to `.value`, leaving `.address` unchanged.
	void ReRead(HANDLE process);
};

// Considers the MemoryObject a candidate if the function returns true. The first parameter is the value from the
// previous scan and the second parameter is the value from the current scan.
template<typename T>
using FilterFn = std::function<bool(const T &, const T &)>;

// Reads a single region of memory. Uses `memory_region.base_address` to know where to read and `memory_region.length`
// to know how much to read. Reallocates and rewrites the values of `memory_region.data`, so it can be passed in as
// nullptr.
SIZE_T ReadRegionData(HANDLE process, MemoryRegion &memory_region);

// Discovers and reads all memory regions from process with R/W permissions. The regions are sorted from lowest base
// address to highest base address addresses.
std::vector<MemoryRegion> InitialScan(HANDLE process);

// Reads the memory regions from the process as dictated by `regions`. Applies the filter for all values, which compares
// the current value to the old value. If nothing matches in that region, it will be removed from `regions`. If it does
// match, that entry in `regions` will be updated with the new process memory. Returns a vector of addresses which
// matched sorted from lowest address to highest.
template<typename T>
std::vector<IntPtr> NextScan(HANDLE process, std::vector<MemoryRegion> &regions, FilterFn<T> keep_if);

// Same as above, but filter will only considers entries contained in `valid_addresses`.
// * This function will remove entries from  `valid_addresses` if they no longer match the filter.
// * This function will remove entries from `regions` if no valid address points there anymore.
// * Neither `valid_addresses` nor `regions` will be re-ordered, the removals will happen in a stable manner.
// * `regions` must be sorted from low to high by base address.
// * `valid_addresses` must be sorted from low to high.
// * The above two constaints should not be a problem if no re-ordering of the vectors happens in your code between
// calls of `InitialScan` and `NextScan`
template<typename T>
void NextScan(HANDLE process, std::vector<MemoryRegion> &regions, std::vector<IntPtr> &valid_addresses,
	FilterFn<T> keep_if);

//
// Implementations of templated functions below...
//

template<typename T>
std::vector<IntPtr> NextScan(HANDLE process, std::vector<MemoryRegion> &regions, FilterFn<T> keep_if)
{
	std::vector<IntPtr> valid_addresses;
	// Swap regions to want to keep to the beginning so all the unwanted regions end up at the back of the vector. Then
	// resize the vector after iterating to truncate the deleted regions. This is stable so the order is preserved.
	size_t new_size = 0;
	for (size_t r = 0; r < regions.size(); ++r) {
		MemoryRegion new_region;
		new_region.base_address = regions[r].base_address;
		new_region.length = regions[r].length;
		ReadRegionData(process, new_region);
		const T *const old_ptr = reinterpret_cast<const T *>(regions[r].data.get());
		const T *const new_ptr = reinterpret_cast<const T *>(new_region.data.get());
		const size_t count = regions[r].length / sizeof(T);
		bool found_at_least_one_valid_address = false;
		for (size_t i = 0; i < count; ++i) {
			if (keep_if(old_ptr[i], new_ptr[i])) {
				found_at_least_one_valid_address = true;
				valid_addresses.push_back(new_region.base_address + (i * sizeof(T)));
			}
		}
		if (found_at_least_one_valid_address) {
			// Replace memory region with the new one.
			regions[r] = std::move(new_region);
			// Keep this element by swapping to front. Only swap if it isn't in the correct position.
			if (new_size != r) {
				std::swap(regions[new_size], regions[r]);
			}
			++new_size;
		}
	}

	regions.resize(new_size);
	return valid_addresses;
}

template<typename T>
void NextScan(HANDLE process, std::vector<MemoryRegion> &regions, std::vector<IntPtr> &valid_addresses,
	FilterFn<T> keep_if)
{
	// Swap elements to want to keep to the beginning so all the unwanted elements end up at the back of the vector.
	// Then resize the vector after iterating to truncate the deleted elements. This is stable so the order is
	// preserved.
	size_t new_size_r = 0;
	size_t new_size_a = 0;
	size_t r = 0;
	size_t a = 0;
	while (r < regions.size() && a < valid_addresses.size()) {
		if (!regions[r].ContainsAddress(valid_addresses[a])) {
			// This region will be skipped (deleted) since it contains no addresses.
			++r;
			continue;
		}
		MemoryRegion new_region;
		new_region.base_address = regions[r].base_address;
		new_region.length = regions[r].length;
		ReadRegionData(process, new_region);
		// Apply the filter for all addresses in this region. Remove the region if nothing valid is found.
		bool found_at_least_one_valid_address = false;
		do {
			const T *const old_ptr = reinterpret_cast<const T *>(regions[r].data.get());
			const T *const new_ptr = reinterpret_cast<const T *>(new_region.data.get());
			const size_t count = regions[r].length / sizeof(T);
			// Puts the absolute address into something that can be indexed into the arrays of type T.
			const size_t translated_index = (valid_addresses[a] - regions[r].base_address) / sizeof(T);
			if (keep_if(old_ptr[translated_index], new_ptr[translated_index])) {
				found_at_least_one_valid_address = true;
				// Keep this address by swapping to front. Only swap if it isn't in the correct position.
				if (new_size_a != a) {
					std::swap(valid_addresses[new_size_a], valid_addresses[a]);
				}
				++new_size_a;
			}
			// Move onto next addresses.
			++a;
		} while (a < valid_addresses.size() && regions[r].ContainsAddress(valid_addresses[a]));
		// If no address passes filter at this region then it will be discarded.
		if (found_at_least_one_valid_address) {
			// Replace memory region with the new one.
			regions[r] = std::move(new_region);
			// Keep this region by swapping to front. Only swap if it isn't in the correct position.
			if (new_size_r != r) {
				std::swap(regions[new_size_r], regions[r]);
			}
			++new_size_r;
		}
		// Move onto the next region.
		++r;
	}

	regions.resize(new_size_r);
	valid_addresses.resize(new_size_a);
}

template<typename T>
void MemoryObject<T>::ReRead(HANDLE process)
{
	SIZE_T bytes_read = 0;
	void *ptr = reinterpret_cast<void *>(address);
	char *dest = reinterpret_cast<char *>(&value);
	if (!ReadProcessMemory(process, ptr, dest, sizeof(T), &bytes_read)) {
		const DWORD ec = GetLastError();
		if (ec != ERROR_PARTIAL_COPY) {
			throw MemoryScannerException("Cannot read process memory", ec, ptr);
		}
	}
	if (bytes_read != sizeof(T)) {
		throw MemoryScannerException("Bytes read differs from memory object size");
	}
}

}  // namespace memory_scanner
