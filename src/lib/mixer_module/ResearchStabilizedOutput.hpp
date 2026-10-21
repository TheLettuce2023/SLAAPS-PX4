#pragma once

#include "mixer_module.hpp"
#include <lib/research_stabilized_manual/StabilizedOutputDriver.hpp>

#if !defined(CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK)
#error ResearchStabilizedOutput requires the explicit mixer hook configuration.
#endif

// Supplied by the serialized output owner. Must check current permission and
// health, without granting authority from actuator messages. Called before
// preparation, immediately before writing, and again after driver completion.
class ResearchOutputPermission
{
public:
	virtual ~ResearchOutputPermission() = default;
	virtual bool outputPermitted(hrt_abstime now) = 0;
};

// Revision must identify the current command publisher lifetime; zero means
// unavailable/exhausted. Frame revisions must come from the same atomic copy
// as their payload, rather than a separate revision query.
class ResearchOutputSource
{
public:
	virtual ~ResearchOutputSource() = default;
	virtual unsigned sourceRevision() = 0;
};

// Immutable out-of-band request, copied coherently by the owner. Its deadline
// limits grant acceptance only; accepted commands retain independent leases.
struct ResearchOutputGrant {
	hrt_abstime session{}; // Request creation time and monotonic session token.
	hrt_abstime deadline_us{}; // Exclusive; never refreshed by consumer retries.
	unsigned source_revision{};
};

struct ResearchMixerResult {
	research_stabilized_manual::DriverDispatch dispatch{};
	uint64_t ticket{};
	bool candidate_valid{};
};

// MAIN bank adapter; no publisher, scheduler, automatic grant or hardware setup.
// Attach before the mixer's first update. The mixer and permission provider must
// outlive this object, and this object must remain alive for every mixer update.
// All calls, including permission changes/revoke, belong to the driver context.
class ResearchStabilizedOutput final : public ResearchOutputTransaction
{
public:
	ResearchStabilizedOutput(MixingOutput &mixer, ResearchOutputPermission &permission, ResearchOutputSource &source) :
		_mixer(mixer), _permission(permission), _source(source) {}
	ResearchStabilizedOutput(const ResearchStabilizedOutput &) = delete;
	ResearchStabilizedOutput &operator=(const ResearchStabilizedOutput &) = delete;
	bool attach();
	// Explicit out-of-band grant after confirmed disarm. The requested source
	// revision must still be current; a frame can never establish this binding.
	research_stabilized_manual::OutputStatus begin(hrt_abstime now, const ResearchOutputGrant &request,
			const research_stabilized_manual::OutputConfiguration &configuration);
	bool prepare(hrt_abstime now, const research_stabilized_manual::OutputFrame *frame,
		     unsigned frame_revision, research_stabilized_manual::OutputCandidate &candidate);
	bool takeResult(ResearchMixerResult &result);
	bool cancel(); // Only before physical I/O or after it is known to have stopped.
	void revoke();
	research_stabilized_manual::DriverState state() const { return _driver.state(); }
	bool candidateValidated() const { return _pending && _validated; }
	bool sourceFault() const { return _source_fault; }
	float control(unsigned channel) const override;
	bool validate(const float *outputs, unsigned count, const actuator_armed_s &armed) override;
	void complete(bool handled, hrt_abstime completed) override;

private:
	static constexpr float kPwmMatchToleranceUs{0.01F};
	bool bankMatches(const research_stabilized_manual::OutputConfiguration &configuration) const;
	bool sourceMatches();
	void invalidateSource();
	MixingOutput &_mixer;
	ResearchOutputPermission &_permission;
	ResearchOutputSource &_source;
	research_stabilized_manual::StabilizedOutputDriver _driver{};
	research_stabilized_manual::OutputConfiguration _configuration{};
	research_stabilized_manual::DriverWrite _write{};
	ResearchMixerResult _result{};
	hrt_abstime _last_command{};
	unsigned _channels{};
	unsigned _source_revision{};
	bool _attached{}, _pending{}, _validated{}, _result_ready{}, _new_command{}, _source_fault{};
};
