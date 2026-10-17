#include "ManualSnapshot.hpp"
#include "BenchControlOwner.hpp"
#include <iostream>

using namespace research_actuator_command;
namespace
{
unsigned checks{}, failures{};
void check(bool value, const char *label)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
BenchConfiguration configuration()
{
	BenchConfiguration c{};
	c.mavlink_instance = 0; c.source_system_id = 255; c.source_component_id = 190;
	c.maximum_cycle_gap_us = 100000; c.neutral_entry_tolerance = 0.05F;
	c.mix = {{{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}}};

	for (unsigned i = 0; i < 4; ++i) {
		c.servos[i] = {1400, 1500, 1600, 10, 1000, 1000};
	}

	return c; // Synthetic timing, mapping and calibration only.
}
struct Source {
	RuntimeTimeUs &time;
	RuntimeTimeUs offset{};
	bool available{true};
	unsigned copies{};
	bool copy(ManualControlSample *sample)
	{
		++copies;
		// Deterministic interleaving: a publisher advances while copying.
		time += 22;

		if (!available) { return false; }

		sample->timestamp_us = sample->timestamp_sample_us = time + offset;
		sample->valid = true; sample->source = ManualControlSource::Mavlink;
		sample->mavlink_instance = 0; sample->source_system_id = 255; sample->source_component_id = 190;
		return true;
	}
};
void interleaving()
{
	RuntimeTimeUs time = 1000000;
	Source source{time};
	ManualControlSample captured{};
	const auto before = time;
	unsigned clock_reads{};
	const auto after = copyManualSnapshot(source, captured, [&]() {
		++clock_reads;
		check(source.copies == 1, "copy completes before clock read");
		return time;
	});
	check(clock_reads == 1 && after == before + 22, "single post-copy clock");
	check(captured.timestamp_us == after, "publication timestamp not rewritten");
	check(captured.timestamp_sample_us == after, "sample timestamp not rewritten");
	BenchControlOwner old_order, fixed_order;
	check(old_order.configure(configuration()) == BenchStatus::Ok, "old order fixture configured");
	check(fixed_order.configure(configuration()) == BenchStatus::Ok, "fixed fixture configured");
	check(old_order.enable(before, captured) == BenchStatus::InputRejected, "old ordering reproduces failure");
	check(old_order.inputStatus() == ManualControlStatus::InvalidTime, "old ordering invalid time");
	check(fixed_order.enable(after, captured) == BenchStatus::Ok, "post-copy enable accepts concurrent valid sample");
	time += 20000;
	const auto cycle_time = copyManualSnapshot(source, captured, [&]() { return time; });
	captured.axes[0] = 0.5F;
	BenchReport report{};
	check(fixed_order.cycle(cycle_time, captured, report) == BenchStatus::Ok, "post-copy active cycle succeeds");
	check(report.normalized[1] > 0, "active cycle produces demand");
	const auto accepted = fixed_order.lastAcceptedReport();
	time += 20000;
	source.offset = 1;
	const auto future_time = copyManualSnapshot(source, captured, [&]() { return time; });
	check(captured.timestamp_us == future_time + 1, "genuine future timestamp remains untouched");
	check(fixed_order.cycle(future_time, captured, report) == BenchStatus::InputRejected, "genuine future input rejected");
	check(fixed_order.inputStatus() == ManualControlStatus::InvalidTime, "future rejection classification");
	check(fixed_order.lastAcceptedReport().timestamp_us == accepted.timestamp_us, "failure preserves accepted report");
	check(fixed_order.neutralRequested(), "future rejection latches neutral");
	source.offset = 0; time += 20000;
	const auto recovered_time = copyManualSnapshot(source, captured, [&]() { return time; });
	check(fixed_order.cycle(recovered_time, captured, report) == BenchStatus::InvalidState, "valid input cannot clear fault");
	source.available = false;
	(void)copyManualSnapshot(source, captured, [&]() { return time; });
	check(!captured.valid && captured.timestamp_us == 0, "missing snapshot cannot reuse previous contents");
}
}
int main()
{
	interleaving();
	std::cout << checks << " snapshot-order checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
