#pragma once

#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <lib/research_stabilized_manual/StabilizedManualControl.hpp>
#include <cstdint>

namespace slaaps
{

constexpr unsigned kCanards = 4;
constexpr unsigned kAxes = 3;
constexpr unsigned kThrustPoints = 5;
constexpr unsigned kScheduleKnots = 2;

enum class ManualInputSource : int32_t { Unconfigured, RcOnly, MavlinkOnly };

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
	int32_t input_axis{-1}; // Selected roll/pitch/yaw slot; explicit permutation.
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

struct ControlScheduleCorner {
	float rate_p[kAxes] {};
	float rate_i[kAxes] {};
	float rate_d[kAxes] {};
	float moment_derivative[kAxes][kCanards] {}; // Signed N m/rad about CG in body FRD.
};

// Optional empirical table. Sensor selection and numerical validity do not
// establish aerodynamic coverage, a qualified envelope or output permission.
struct ControlScheduleConfiguration {
	int32_t enabled{};
	int32_t airspeed_instance{-1}; // Raw airspeed.indicated_airspeed_m_s, never groundspeed.
	int32_t air_data_instance{-1}; // vehicle_air_data.rho.
	int32_t battery_instance{-1}; // battery_status.voltage_v.
	int32_t battery_id{};
	int32_t maximum_age_us{};
	float indicated_airspeed_m_s[kScheduleKnots] {};
	float edf_command[kScheduleKnots] {}; // Normalized owner command, not inferred thrust.
	float density_min{};
	float density_max{};
	float voltage_min{};
	float voltage_max{};
	float filter_time_s{};
	float airspeed_filter_rate{}; // m/s per second, filtered coordinate slew limit.
	float edf_filter_rate{}; // Normalized command/s, filtered coordinate slew limit.
	float airspeed_transient_rate{}; // Maximum accepted raw IAS change, m/s per second.
	float edf_transient_rate{}; // Maximum accepted owner-command change per second.
	ControlScheduleCorner corner[kScheduleKnots][kScheduleKnots] {}; // [IAS knot][EDF knot].
};

// Transport source selection and freshness only. Enabling this group grants
// no trusted session, clock map, acceleration or actuator authority.
struct CompanionConfiguration {
	int32_t enabled{};
	int32_t mavlink_instance{-1};
	int32_t source_system{};
	int32_t source_component{};
	int32_t source_id{}; // Exact positive 31-bit identity from the request payload.
	int32_t maximum_age_us{};
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
	int32_t manual_source{}; // 0 unconfigured, 1 RC only, 2 pinned MAVLink only.
	int32_t manual_instance{-1};
	int32_t manual_system{};
	int32_t manual_component{};
	float manual_throttle_low{}; // Raw selected throttle at zero EDF demand.
	float manual_throttle_high{}; // Raw selected throttle at full EDF demand.
	int32_t manual_age_ms{};
	int32_t state_age_ms{};
	int32_t state_skew_ms{};
	int32_t selected_status_age_us{};
	int32_t actuator_permission_age_us{};
	int32_t activation_ack_timeout_us{};
	int32_t first_command_budget_us{};
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
	float edf_commission_max{}; // Explicit commissioning ceiling; zero denies.
	ControlScheduleConfiguration schedule{};
	CompanionConfiguration companion{};
};

enum class ConfigurationUse : uint8_t { Bench, StabilizedManual, EdfCommissioning = 2 };
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

// Independent timing-group check. All-zero timing may be staged in a profile,
// but cannot activate. Configured bounds are not runtime timing qualification.
bool manualActivationTimingConfigured(const VehicleConfiguration &configuration);
bool benchActivationTimingConfigured(const VehicleConfiguration &configuration);

// Enabled-table checks, including fixed moment normalization and servo limits
// at every corner. Disabled tables may be staged. No envelope qualification,
// interpolation, sensor admission or flight permission is supplied here.
ConfigurationResult validateControlSchedule(const VehicleConfiguration &configuration);

// Independent source-group validation, including when no vehicle model exists.
// Disabled in-range fields may be staged; enabled fields must all be explicit.
ConfigurationResult validateCompanionConfiguration(const VehicleConfiguration &configuration);

// Interpolation is valid only at the declared curve voltage/condition.
// Flight admission must establish that condition separately.
bool thrustToCommand(const VehicleConfiguration &configuration, float thrust_n, float &command);

// Raw selected throttle at the declared zero/full endpoints, including reversal.
// Source/freshness/duplicate validation remains with ManualControlInput. Failure
// preserves command; no clipping, ownership, request or permission is supplied.
bool normalizeManualThrottle(float raw, float low, float high, float &command);

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
