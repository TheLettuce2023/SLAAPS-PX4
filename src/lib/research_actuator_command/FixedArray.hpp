#pragma once

#include <cstddef>

namespace research_actuator_command
{

// Minimal fixed-size contiguous container for the shared host/NuttX control
// foundation. PX4's NuttX C++ profile does not provide the standard <array>
// header. This type deliberately exposes only the operations used by H1-H3.
template<typename T, std::size_t Size>
struct FixedArray {
	using value_type = T;

	T elements[Size] {};

	constexpr T &operator[](std::size_t index)
	{
		return elements[index];
	}

	constexpr const T &operator[](std::size_t index) const
	{
		return elements[index];
	}

	constexpr T *data()
	{
		return elements;
	}

	constexpr const T *data() const
	{
		return elements;
	}

	constexpr T *begin()
	{
		return elements;
	}

	constexpr const T *begin() const
	{
		return elements;
	}

	constexpr T *end()
	{
		return elements + Size;
	}

	constexpr const T *end() const
	{
		return elements + Size;
	}

	constexpr std::size_t size() const
	{
		return Size;
	}
};

template<typename T, std::size_t Size>
constexpr bool operator==(const FixedArray<T, Size> &left, const FixedArray<T, Size> &right)
{
	for (std::size_t index = 0; index < Size; ++index) {
		if (!(left[index] == right[index])) {
			return false;
		}
	}

	return true;
}

template<typename T, std::size_t Size>
constexpr bool operator!=(const FixedArray<T, Size> &left, const FixedArray<T, Size> &right)
{
	return !(left == right);
}

} // namespace research_actuator_command
