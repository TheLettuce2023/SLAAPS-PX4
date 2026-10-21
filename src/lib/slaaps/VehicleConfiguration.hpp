#pragma once

#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <lib/research_stabilized_manual/StabilizedManualControl.hpp>
#include <cstdint>

namespace slaaps
{

constexpr unsigned kCanards = 4;
constexpr unsigned kAxes = 3;
constexpr unsigned kThrustPoints = 5;

// Body FRD, inertia about CG. All physical defaults are deliberately absent.
// Parameter metadata and native readers share VehicleParameters.def.
struct CanardConfiguration {
	float moment_derivative[kAxes] {}; // N m/rad, one measured airflow condition
	float derivative_uncertainty[kAxes] {};
	float deflection_slope{}; // signed rad/normalized unit
	float slope_uncertainty{};
	float trim{};
	float minimum{};
	float maximum{};
	float slew_positive{}; // normalized units/s
	float slew_negative{};
	float pwm_negative{}; // us at -maximum_deflection_deg
	float pwm_neutral{};
	float pwm_positive{};
	float maximum_deflection_deg{};
	float lag_s{};
	float latency_s{};
	float local_airflow{}; // measured m/s, not inferred from EDF thrust
	float bench_mix[kAxes] {};
};

struct AxisConfiguration {
	float moment_scale{}; // N m/normalized moment
	float sign{};
	float rate_limit{};
	float setpoint_accel_limit{};
	float measured_rate_limit{};
	float measured_accel_limit{};
	float attitude_gain{};
	float rate_p{};
	float rate_i{};
	float rate_d{};
	float integrator_limit{};
	float moment_limit{};
	float allocation_weight{};
};

struct VehicleConfiguration {
	int32_t data_revision{}; // operator record identifier; never flight acceptance
	float mass{};
	float cg[kAxes] {};
	float inertia[6] {}; // xx, yy, zz, xy, xz, yz (actual tensor entries)
	float airflow{};
	float angle_of_attack{};
	float sideslip{};
	float condition_thrust{};
	CanardConfiguration canard[kCanards] {};
	AxisConfiguration axis[kAxes] {};
	int32_t manual_instance{-1};
	int32_t manual_system{};
	int32_t manual_component{};
	int32_t manual_age_ms{};
	int32_t state_age_ms{};
	int32_t state_skew_ms{};
	int32_t cycle_min_us{};
	int32_t cycle_max_us{};
	int32_t bench_gap_ms{};
	int32_t output_lease_ms{};
	float centered_enter{};
	float centered_exit{};
	float bench_deadzone{};
	float quaternion_tolerance{};
	float regularization{};
	float minimum_row_norm{};
	float minimum_independence{};
	float saturation_tolerance{};
	float edf_thrust[kThrustPoints] {}; // N at normalized commands 0,.25,.5,.75,1
	float edf_voltage{}; // measured supply voltage for this curve
	float edf_rise{}; // normalized command/s
	float edf_fall{};
	float edf_entry_max{}; // normalized low-throttle admission limit
};

enum class ConfigurationUse : uint8_t { Bench, StabilizedManual };
enum class ConfigurationFault : uint8_t {
	None, ParameterRange, MissingRecord, InvalidInertia, InvalidServo,
	InvalidTiming, InvalidEffectiveness, InvalidController, InvalidThrustCurve,
	Active, NoCandidate
};

struct ConfigurationResult {
	ConfigurationFault fault{ConfigurationFault::None};
	const char *parameter{nullptr};
	bool valid() const { return fault == ConfigurationFault::None; }
};

// Scalar metadata checks always apply, even to fields unused by a mode.
// Bench does not require the aerodynamic/propulsion model.
ConfigurationResult validate(const VehicleConfiguration &configuration, ConfigurationUse use);
bool equal(const VehicleConfiguration &left, const VehicleConfiguration &right);
uint64_t fingerprint(const VehicleConfiguration &configuration);
bool makeBenchProfile(const VehicleConfiguration &configuration,
		      research_actuator_command::BenchOutputProfile &profile);
bool makeManualConfiguration(const VehicleConfiguration &configuration,
			     research_stabilized_manual::Configuration &control);

// Interpolation is valid only at the declared curve voltage/condition.
// Flight admission must establish that condition separately.
bool thrustToCommand(const VehicleConfiguration &configuration, float thrust_n, float &command);

// Complete candidates only. Native parameter capture must supply a coherent
// transaction; this store does not pretend individual param_get calls are atomic.
// Any observed edit latches invalidation until disable + deliberate new commit.
class ConfigurationStore
{
public:
	ConfigurationResult commit(const VehicleConfiguration &candidate, ConfigurationUse use);
	bool activate();
	void observe(const VehicleConfiguration &candidate);
	void invalidate();
	void disable();
	const VehicleConfiguration &configuration() const { return _configuration; }
	uint64_t identity() const { return _identity; }
	ConfigurationUse use() const { return _use; }
	bool eligible() const { return _active && !_invalidated; }
	bool hasCandidate() const { return _candidate; }
private:
	VehicleConfiguration _configuration{};
	uint64_t _identity{};
	ConfigurationUse _use{ConfigurationUse::Bench};
	bool _candidate{};
	bool _active{};
	bool _invalidated{};
};

} // namespace slaaps
