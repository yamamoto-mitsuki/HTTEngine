#pragma once
#include <cstdint>


template<class T> 
struct Handle {
	uint32_t index = 0;
	uint32_t generation = 0; // 破棄されるたびに増える

	bool IsValid() const { return generation != 0; }
	bool operator==(const Handle&) const = default;
};