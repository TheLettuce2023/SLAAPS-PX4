#include "ObserverProfile.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace research_stabilized_manual;
namespace
{
unsigned checks{}, failures{};
void check(bool ok, const char *name)
{
	++checks;

	if (!ok) { ++failures; printf("FAIL %s\n", name); }
}

// Explicit test-only numbers; no corresponding embedded firmware defaults.
const char *fixture =
	"SLAAPS_STABILIZED_OBSERVER_V1 2 255 190 "
	"100000 100000 50000 4000 60000 1100000 600000 "
	"0.01 0.04 0.06 "
	"1 -1 1  1 1 1  20 20 20  20 20 20  100 100 100  4 4 4 "
	"0.4 0.8 0.8  0.05 0.1 0.1  0.003 0.006 0.006  0.2 0.2 0.2  1 1 1 "
	"0.25 -0.25 0.25 -0.25  0.25 0.25 -0.25 -0.25  0.25 -0.25 -0.25 0.25 "
	"0 0 0 0  -1 -1 -1 -1  1 1 1 1  5 5 5 5  5 5 5 5 "
	"1 1 1  0.00001 0.01 0.01 0.001";

bool parse(const std::string &text, ObserverProfile &profile)
{
	FILE *stream = tmpfile();

	if (!stream) { return false; }

	fwrite(text.data(), 1, text.size(), stream);
	rewind(stream);
	const bool result = readObserverProfile(stream, profile);
	fclose(stream);
	return result;
}

std::string join(const std::vector<std::string> &tokens)
{
	std::string text;

	for (const auto &entry : tokens) { text += entry + " "; }

	return text;
}
}

int main(int argc, char *argv[])
{
	// The native integration runner requests this explicit fixture file.
	if (argc == 2) {
		FILE *out = fopen(argv[1], "wx");

		if (!out) { return 1; }

		fprintf(out, "%s\n", fixture);
		return fclose(out) == 0 ? 0 : 1;
	}

	ObserverProfile profile{};
	check(parse(fixture, profile), "complete fixture parsed");
	check(profile.control.mavlink_instance == 2 && profile.control.source_system_id == 255
	      && profile.control.source_component_id == 190, "identity preserved");
	check(profile.control.fixed_simulated_thrust_n == 0.0F, "no propulsion in observer");
	check(profile.control.allocation.effectiveness[2][1] == -0.25F, "matrix order preserved");
	std::istringstream input(fixture);
	std::vector<std::string> tokens;

	for (std::string word; input >> word;) { tokens.push_back(word); }

	check(tokens.size() == 86, "versioned token count");

	for (unsigned i = 0; i < tokens.size(); ++i) {
		auto changed = tokens;
		changed[i] = "nan";
		profile.maximum_permission_age_us = 123;
		check(!parse(join(changed), profile) && profile.maximum_permission_age_us == 123, "nonfinite rejection preserves caller");
		changed = tokens;
		changed.resize(i);
		check(!parse(join(changed), profile), "truncated profile rejected");
	}

	for (const char *value : {"-1", "1.5", "1e3", "18446744073709551616", "4294967296"}) {
		auto changed = tokens;
		changed[4] = value;
		check(!parse(join(changed), profile), "integer syntax/range rejects");
	}
	check(!parse(std::string(fixture) + " 1", profile), "extra token rejected");
	check(!parse(std::string(80, '1') + " " + fixture, profile), "oversize token rejected");
	check(!parse(std::string(fixture) + std::string(1, '\0'), profile), "embedded NUL rejected");
	check(!readObserverProfile(nullptr, profile), "null stream rejected");
	check(parse(std::string("\n\t") + fixture + "\r\n ", profile), "whitespace supported");
	StabilizedManualControl owner;
	check(owner.configure(profile.control) == Status::Ok, "zero thrust control configured");
	ManualSample manual{};
	manual.valid = true;
	manual.source = research_actuator_command::ManualControlSource::Mavlink;
	manual.mavlink_instance = 2;
	manual.source_system_id = 255;
	manual.source_component_id = 190;
	manual.timestamp_us = manual.timestamp_sample_us = 1000000;
	StateSample sample{};
	sample.valid = true;
	sample.attitude = Quaternion(1.0F, 0.0F, 0.0F, 0.0F);
	sample.source_epoch = 1;
	sample.attitude_timestamp_us = sample.attitude_sample_us = sample.rate_timestamp_us = sample.rate_sample_us = 1000000;
	check(owner.enable(1000000, manual, sample) == Status::Ok, "zero thrust centered hold entry");
	manual.timestamp_us = manual.timestamp_sample_us = 1004000;
	sample.attitude_timestamp_us = sample.attitude_sample_us = sample.rate_timestamp_us = sample.rate_sample_us = 1004000;
	Report report{};
	check(owner.cycle(1004000, manual, sample, report) == Status::Ok
	      && report.state == State::Hold && report.fixed_simulated_thrust_n == 0.0F, "wind-only hold has zero thrust");

	for (float invalid : {-1.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
		owner.disable();
		profile.control.fixed_simulated_thrust_n = invalid;
		check(owner.configure(profile.control) == Status::InvalidConfiguration, "invalid thrust still rejected");
	}
	printf("observer profile: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
