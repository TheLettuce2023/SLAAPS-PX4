#pragma once

#include <px4_platform_common/px4_config.h>
#if !defined(RESEARCH_C1C_CAPTURE) || RESEARCH_C1C_CAPTURE != 1 || !defined(__linux__) \
	|| !defined(__PX4_POSIX) || !defined(CONFIG_ARCH_BOARD_PX4_SITL)
#error "C1C producer is restricted to the explicit Linux POSIX SITL capture build"
#endif
#if defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "C1C requires native non-lockstep, non-proxy clocks"
#endif

#include "StateCaptureProducer.hpp"
#include <uORB/Publication.hpp>
#include <uORB/topics/research_state_receipt.h>
#include <uORB/topics/research_state_summary.h>

namespace research_sitl
{
namespace state_capture
{

Bootstrap &captureBootstrap() noexcept;
bool captureProductionFinished() noexcept;

class Px4StateReceiptProducer
{
public:
	Px4StateReceiptProducer() noexcept : producer_(captureBootstrap()) {}
	void initial(uint8_t arm, uint8_t nav) noexcept;
	void transition(State before, uint8_t arm, uint8_t nav) noexcept;
	void sample(uint8_t arm, uint8_t nav) noexcept;
	void finish(uint8_t arm, uint8_t nav) noexcept;

private:
	void publish(const std::optional<Receipt> &receipt) noexcept;
	Producer producer_;
	uORB::Publication<research_state_receipt_s> receipts_{ORB_ID(research_state_receipt)};
	uORB::Publication<research_state_summary_s> summary_{ORB_ID(research_state_summary)};
	bool finished_{};
};

} // namespace state_capture
} // namespace research_sitl
