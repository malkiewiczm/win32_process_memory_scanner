target_include_directories (memory_scan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(memory_scan PUBLIC
	example.cpp
	memory_scanner.cpp
	memory_scanner.hpp
	memory_scanner_exception.cpp
	memory_scanner_exception.hpp
)
