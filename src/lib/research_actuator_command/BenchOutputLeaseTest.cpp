#include "BenchOutputLease.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace research_actuator_command;
namespace
{
unsigned checks{}, failures{};
void check(bool value, const char *label)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.0001F; }
std::vector<std::string> tokens()
{
	// Synthetic fixture only. No installed mix, calibration or timing claim.
	std::vector<std::string> result{"1", "0", "255", "190", "40000", "0.05", "0.05", "100000"};

	for (unsigned i = 0; i < 4; ++i) {
		for (const auto *token : {"1400", "1500", "1600", "10", "1000", "1000", "1", "-1", "1"}) { result.emplace_back(token); }
	}

	return result;
}
bool parse(const std::vector<std::string> &values, BenchOutputProfile &profile)
{
	FILE *file = tmpfile();

	if (!file) { return false; }

	for (const auto &token : values) { fprintf(file, "%s ", token.c_str()); }

	rewind(file);
	const bool ok = readBenchOutputProfile(file, profile);
	fclose(file);
	return ok;
}
void profiles()
{
	BenchOutputProfile profile{};
	check(parse(tokens(), profile), "valid explicit fixture");
	check(profile.control.source_system_id == 255 && profile.lease_us == 100000, "profile fields");

	for (unsigned i = 0; i < tokens().size(); ++i) {
		auto invalid = tokens();
		invalid.resize(i);
		check(!parse(invalid, profile), "truncated profile");
		check(profile.lease_us == 100000, "failure preserves caller");
		invalid = tokens(); invalid[i] = "nan";
		check(!parse(invalid, profile), "nonfinite profile");
	}

	for (const char *bad : {"-1", "1.5", "1e999", "0junk", "999999999999999999999999999999999999999999999999999999999999"}) {
		auto invalid = tokens(); invalid[1] = bad;
		check(!parse(invalid, profile), "invalid integer token");
	}
	auto invalid = tokens(); invalid.push_back("extra");
	check(!parse(invalid, profile), "trailing token");
	invalid = tokens(); invalid[7] = "20000";
	check(!parse(invalid, profile), "lease below cycle gap");
	invalid = tokens(); invalid[7] = "500001";
	check(!parse(invalid, profile), "lease above accepted input age");
	invalid = tokens(); invalid[6] = "1";
	check(!parse(invalid, profile), "invalid deadzone");
	check(!readBenchOutputProfile(nullptr, profile), "null profile stream");
}
void shaping()
{
	for (float x : {-1.0F, -0.525F, -0.05F, 0.0F, 0.05F, 0.525F, 1.0F}) {
		const float shaped = benchDeadzone(x, 0.05F);
		check(std::isfinite(shaped) && std::fabs(shaped) <= 1, "bounded shaping");
		check(near(shaped, -benchDeadzone(-x, 0.05F)), "odd shaping");
	}
	check(near(benchDeadzone(0.525F, 0.05F), 0.5F), "rescaled deadzone");
	check(near(benchDeadzone(0.05F, 0.05F), 0), "inclusive edge");
	check(std::isnan(benchDeadzone(1.01F, 0.05F)), "does not conceal invalid axis");
	check(std::isnan(benchDeadzone(NAN, 0.05F)), "nonfinite axis");
	check(std::isnan(benchDeadzone(0, 1)), "bad shaping configuration");
	BenchOutputProfile profile{};
	check(parse(tokens(), profile), "PWM fixture");
	BenchVector control{};
	check(benchPwmToControl({500, 1400, 1600, 2500}, profile.control, control), "PWM full range");
	check(near(control[0], -1) && near(control[1], -0.1F)
	      && near(control[2], 0.1F) && near(control[3], 1), "driver conversion");

	for (float bad : {499.0F, 2501.0F, std::numeric_limits<float>::quiet_NaN()}) {
		check(!benchPwmToControl({1500, 1500, 1500, bad}, profile.control, control), "invalid PWM rejected");
		check(near(control[0], -1) && near(control[3], 1), "PWM rejection preserves vector");
	}
	profile.control.servos[0].neutral_pwm_us = 1400;
	check(benchPwmToControl({1400, 1500, 1500, 1500}, profile.control, control), "non-midpoint neutral");
	check(near(control[0], 0), "calibrated neutral");
	profile.control.servos[0].neutral_pwm_us = 500;
	check(!benchPwmToControl({1400, 1500, 1500, 1500}, profile.control, control), "degenerate center rejected");
}
BenchOutputFrame frame(RuntimeTimeUs time = 1000000)
{
	BenchOutputFrame f{};
	f.timestamp = time; f.session = 1000000; f.lease_us = 100000;
	f.control = {0.1F, -0.2F, 0.3F, -0.4F}; f.enabled = true;
	return f;
}
void leases()
{
	BenchOutputLease lease;
	check(std::isnan(lease.value(1000000, 0)), "boot disarmed");
	lease.update(1000000, frame());

	for (unsigned i = 0; i < 4; ++i) { check(near(lease.value(1099999, i), frame().control[i]), "four controls"); }

	check(near(lease.value(1100000, 0), 0), "exact expiry neutral");
	check(lease.faulted(), "expiry latched");
	lease.update(1100001, frame(1100001));
	check(near(lease.value(1100001, 0), 0), "same session cannot recover");
	auto next = frame(1200000); next.session = next.timestamp;
	lease.update(next.timestamp, next);
	check(near(lease.value(next.timestamp, 0), 0.1F), "new explicit session");
	check(std::isnan(lease.value(next.timestamp, 4)), "no fifth EDF channel");

	for (unsigned variant = 0; variant < 9; ++variant) {
		BenchOutputLease instance; instance.update(1000000, frame());
		auto bad = frame(1020000);

		switch (variant) {
		case 0: bad.enabled = false; break;

		case 1: bad.control[2] = NAN; break;

		case 2: bad.control[3] = 1.1F; break;

		case 3: bad.timestamp = 1000000; break;

		case 4: bad.timestamp = 1030000; break;

		case 5: bad.lease_us = 0; break;

		case 6: bad.session = 999999; break;

		case 7: bad.lease_us = 500001; break;

		case 8: bad.session = 1030000; break;
		}

		instance.update(1020000, bad);
		check(instance.faulted(), "invalid/revoke latch");

		for (unsigned i = 0; i < 4; ++i) { check(near(instance.value(1020000, i), 0), "whole vector neutral"); }
	}

	BenchOutputLease late;
	late.update(1000000, frame()); late.update(1100000, frame(1100000));
	check(late.faulted(), "late update cannot skip expiry poll");
	BenchOutputLease backward;
	backward.update(1000000, frame());
	check(near(backward.value(999999, 0), 0), "clock regression neutral");
	BenchOutputLease permission;
	permission.update(1000000, frame()); permission.revoke();
	permission.update(1020000, frame(1020000));
	check(near(permission.value(1020000, 0), 0), "safety interruption cannot resume same session");
}
}
int main()
{
	profiles(); shaping(); leases();
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
