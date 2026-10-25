#pragma once

#include <cstdint>

#if defined(__PX4_NUTTX)
#include <nuttx/config.h>
#endif

#if defined(__linux__) || (defined(__PX4_NUTTX) && defined(CONFIG_STM32H7_RNG) && defined(CONFIG_DEV_RANDOM))
#include <sys/types.h>
extern "C" {
#include <sys/random.h>
}
#endif

namespace slaaps
{

// Call once from module initialization before scheduling. No software PRNG,
// device identity or elapsed-time fallback can establish a controller nonce.
// Random uniqueness is probabilistic; preserve this identity/replay namespace
// for the owner's lifetime and obtain a new identity after an owner restart.
inline bool generateCompanionIdentity(uint64_t &identity)
{
#if defined(__PX4_NUTTX) && defined(CONFIG_STM32H7_RNG) && defined(CONFIG_DEV_RANDOM)

	uint64_t candidate{};
	const ssize_t count = getrandom(&candidate, sizeof(candidate), GRND_RANDOM);

#elif defined(__linux__) && !defined(__PX4_NUTTX)

	uint64_t candidate{};
	const ssize_t count = getrandom(&candidate, sizeof(candidate), GRND_NONBLOCK);

#else

	(void)identity;
	return false;

#endif

#if (defined(__PX4_NUTTX) && defined(CONFIG_STM32H7_RNG) && defined(CONFIG_DEV_RANDOM)) || (defined(__linux__) && !defined(__PX4_NUTTX))

	if (count != static_cast<ssize_t>(sizeof(candidate)) || candidate == 0) { return false; }

	identity = candidate;
	return true;

#endif
}

} // namespace slaaps
