#include "memory_scanner_exception.hpp"

#define STRICT
#define NOMINMAX
#include <Windows.h>

#include <exception>
#include <source_location>
#include <string>
#include <string_view>

namespace memory_scanner
{
namespace
{

constexpr const char *const hex_digit_lookup = "0123456789ABCDEF";

// Not designed to be called with an empty list or pieces of size 0.
std::string StringCat(std::initializer_list<std::string_view> pieces)
{
	size_t new_size = 0;
	for (const std::string_view piece : pieces) {
		new_size += piece.size();
	}
	std::string result;
	result.resize(new_size);
	char *append_at = &result[0];
	for (const std::string_view piece : pieces) {
		const size_t len = piece.size();
		std::memcpy(append_at, piece.data(), len);
		append_at += len;
	}
	return result;
}

std::string_view Hex32(std::uint_least32_t x, char *const buf)
{
	for (int i = 7; i >= 0; --i) {
		buf[i] = hex_digit_lookup[x & 0xF];
		x >>= 4;
	}
	return std::string_view(buf, 8);
}

std::string_view Hex64(std::uint_least64_t x, char *const buf)
{
	for (int i = 15; i >= 0; --i) {
		buf[i] = hex_digit_lookup[x & 0xF];
		x >>= 4;
	}
	return std::string_view(buf, 16);
}

std::string ConcatScannerExceptionMessage(std::string_view message, DWORD ec, const void *const void_ptr,
	const std::source_location &source_location)
{
	std::uint_least64_t ptr = 0;
	std::memcpy(&ptr, &void_ptr, 8);
	const std::string source_location_line = std::to_string(source_location.line());
	if (ec == 0 && ptr == 0) {
		const char *const file_name = source_location.file_name();
		return StringCat({ message, " (", source_location.file_name(), ":", source_location_line, ")" });
	}
	if (ptr == 0) {
		char ec_buf[8];
		return StringCat({ message, "; error code 0x", Hex32(ec, ec_buf), " (", source_location.file_name(), ":",
			source_location_line, ")" });
	}
	char ec_buf[8];
	char ptr_buf[16];
	return StringCat({ message, "; ptr = 0x", Hex64(ptr, ptr_buf), "; error code 0x", Hex32(ec, ec_buf), " (",
		source_location.file_name(), ":", source_location_line, ")" });
}

}  // namespace

MemoryScannerException::MemoryScannerException(std::string_view message_, const DWORD windows_error_code_,
	const void *const ptr, std::source_location source_location_)
	: message(ConcatScannerExceptionMessage(message_, windows_error_code_, ptr, source_location_)),
	  windows_error_code(windows_error_code_),
	  source_location(source_location_)
{
}

}  // namespace memory_scanner
