#pragma once

#define STRICT
#define NOMINMAX
#include <Windows.h>

#include <exception>
#include <source_location>
#include <string>
#include <string_view>

namespace memory_scanner
{

// Used to communicate errors back.
class MemoryScannerException : public std::exception
{
public:
	// ptr is an optional argument that will communicate a memory address along with your message.
	MemoryScannerException(std::string_view message_, DWORD windows_error_code_ = 0, const void *ptr = nullptr,
		std::source_location source_location_ = std::source_location::current());

	const char *what() const noexcept override { return message.c_str(); }

	const std::string message;
	const DWORD windows_error_code;
	const std::source_location source_location;
};

}  // namespace memory_scanner
