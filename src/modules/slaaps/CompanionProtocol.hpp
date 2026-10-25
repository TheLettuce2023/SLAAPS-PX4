#pragma once

#include <lib/slaaps/VehicleConfiguration.hpp>
#include <lib/slaaps/AccelerationFeedback.hpp>
#include <lib/research_acceleration/WireStatus.hpp>
#include <lib/research_stabilized_manual/SelectedStateObservation.hpp>
#include <parameters/param.h>
#include <uORB/Publication.hpp>
#include <uORB/ResearchSubscription.hpp>
#include <uORB/topics/slaaps_companion_control.h>
#include <uORB/topics/slaaps_companion_packet.h>
#include <uORB/topics/slaaps_companion_status.h>
#include <uORB/topics/slaaps_companion_tx.h>
#include <uORB/topics/slaaps_vehicle_state.h>

#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
struct SlaapsNativeTest;
#endif

namespace slaaps
{
struct AccelerationControlInput;

// One serialized protocol owner. Controller access revalidates live evidence;
// no eligible-demand cache or actuator/controller publisher exists here.
class CompanionProtocol
{
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
	friend struct ::SlaapsNativeTest;
#endif
public:
	CompanionProtocol() = default;
	~CompanionProtocol();
	CompanionProtocol(const CompanionProtocol &) = delete;
	CompanionProtocol &operator=(const CompanionProtocol &) = delete;
	bool initialize(uint64_t seed);
	void update(const VehicleConfiguration *configuration, uint64_t identity, int32_t apply,
		    int8_t manual_mode, int8_t acceleration_mode, bool allow_entry_transitions, uint64_t now);
	void stop();
	// Serialized supervisor only. Reobserves live state and accesses the full
	// authenticated request; failure preserves output. Worker-owned actuator
	// history and operating condition must be supplied separately before use.
	bool captureControlInput(uint64_t configuration, AccelerationControlInput &input);
	bool captureControlInput(uint64_t configuration,
				 const research_stabilized_manual::SelectedStateObservationSnapshot &selected,
				 AccelerationControlInput &input);
	// Valid only immediately after capture on this serialized owner. False
	// capture may wait only for ACK/new request, never a source/config fault.
	bool awaitingControlInput() const { return _capture_pending; }
	// Last capture only: exact observed ordinary disarm permits a stop, never
	// output or reentry. The original evidence bound is exclusive and immutable.
	bool observedOrdinaryDisarm(uint64_t now) const
	{
		return _capture_disarm_deadline && now >= _clock && now < _capture_disarm_deadline;
	}
	bool awaitingAccelerationObservation(uint64_t transition_us) const
	{
		return _capture_mode_pending && transition_us > _witness.timestamp && transition_us <= _status.timestamp;
	}
	const slaaps_companion_status_s &status() const { return _status; } // Serialized owner only.
	void printStatus() const; // Reads the published snapshot, safe for the CLI thread.
private:
	using Adapter = research_acceleration::WireAdapter;
	using State = research_acceleration::TrustedStateSample;
	using Session = research_acceleration::DecodedSessionV1;
	using Request = research_acceleration::DecodedRequestV1;
	using Ingress = research_acceleration::SessionIngressVerdict;
	using WireStatus = research_acceleration::WireStatusV1;
	void resetAdapter();
	bool configure(const VehicleConfiguration &configuration, uint64_t identity, int32_t apply);
	bool observe(uint64_t &now, actuator_armed_s *permission = nullptr);
	bool captureControlInputImpl(uint64_t configuration,
				     const research_stabilized_manual::SelectedStateObservationSnapshot *selected,
				     AccelerationControlInput &input);
	bool acceptEntryTransition(const slaaps_vehicle_state_s &previous, uint64_t previous_deadline, uint64_t now);
	void packet(const slaaps_companion_packet_s &packet, unsigned revision, uint64_t &now);
	void action(int32_t command, uint64_t &now);
	void closeMap(bool change_binding = true);
	bool advance(uint64_t &counter);
	bool disarmed(uint64_t now);
	bool freezeMap(const slaaps_companion_packet_s &packet, uint64_t now);
	bool send(const WireStatus &status, uint64_t now, bool barrier = false);
	void reports(const research_acceleration::AdapterResult &result, uint64_t now);
	void reports(const research_acceleration::AdminResult &result, uint64_t now);
	void reports(const research_acceleration::IngressResult &result, uint64_t now);
	template<typename Report> void report(const Report &report, uint64_t now, bool barrier);
	void publishControl(uint64_t now);
	void publishStatus(uint64_t now);
	void refreshStatus(uint64_t now);
	CompanionConfiguration _configuration{};
	research_stabilized_manual::Configuration _numerical{};
	AccelerationFeedback _feedback{};
	AccelerationFeedbackSample _feedback_sample{};
	AccelerationFeedbackResult _feedback_result{AccelerationFeedbackResult::Disabled};
	// Disabled feedback preserves transport-only commissioning. A future
	// controller must require enabled, Ready feedback as a separate admission
	// prerequisite; a protocol session alone never grants control authority.
	bool _feedback_required{};
	bool _capture_pending{};
	bool _capture_mode_pending{};
	// Fixed in-object storage: immutable adapter reconstruction needs no heap
	// and no unavailable NuttX C++ optional/type-trait headers.
	alignas(Adapter) unsigned char _adapter_storage[sizeof(Adapter)] {};
	Adapter *_adapter{};
	uORB::ResearchSubscription _input[4] {
		{ORB_ID::slaaps_companion_packet_0}, {ORB_ID::slaaps_companion_packet_1},
		{ORB_ID::slaaps_companion_packet_2}, {ORB_ID::slaaps_companion_packet_3}
	};
	uORB::ResearchSubscription _vehicle{ORB_ID::slaaps_vehicle_state};
	research_stabilized_manual::SelectedStateObservation _selected{};
	uORB::Publication<slaaps_companion_control_s> _control{ORB_ID(slaaps_companion_control)};
	uORB::Publication<slaaps_companion_tx_s> _tx{ORB_ID(slaaps_companion_tx)};
	uORB::Publication<slaaps_companion_tx_s> _barrier_tx{ORB_ID(slaaps_companion_barrier)};
	uORB::Publication<slaaps_companion_status_s> _publication{ORB_ID(slaaps_companion_status)};
	param_t _action{PARAM_INVALID};
	State _state{};
	State _observed{};
	research_acceleration::AdapterSnapshot _snapshot{};
	slaaps_vehicle_state_s _witness{};
	research_stabilized_manual::StateSample _previous_state{};
	unsigned _state_revisions[research_stabilized_manual::PublisherBinding::kTopics] {};
	Session _proposal{};
	Ingress _proposal_ingress{};
	WireStatus _barrier{};
	slaaps_companion_status_s _status{};
	uint64_t _next_identity{}, _identity{}, _configuration_identity{}, _binding{}, _map_epoch{}, _admin_sequence{};
	uint64_t _started{}, _clock{}, _state_deadline{}, _witness_deadline{}, _connection_deadline{}, _map_deadline{};
	uint64_t _proposal_deadline{}, _prepared_session{}, _last_barrier_send{}, _packet_sequence{};
	uint64_t _status_age{}, _permission_age{}, _control_deadline{};
	uint64_t _capture_disarm_deadline{};
	uint32_t _receiver_revision{}, _signing_generation{}, _commander_revision{}, _estimator_epoch{}, _gyro{}, _accel{};
	uint8_t _receiver_system{}, _receiver_component{};
	int32_t _apply{};
	int8_t _manual_mode{-1};
	int8_t _acceleration_mode{-1};
	// A successful disarmed Grant may authorize only the next healthy entry.
	// These are protocol-transition allowances, never actuator/flight permission.
	bool _allow_entry_transitions{}, _entry_authorized{}, _entry_arm_used{}, _entry_acceleration_used{};
	bool _initialized{}, _configured{}, _continuity_fault{}, _prepared{}, _state_seen{}, _previous_valid{}, _have_barrier{}, _control_expired{};
};

} // namespace slaaps
