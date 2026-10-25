#pragma once

#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <lib/research_stabilized_manual/StabilizedManualControl.hpp>
#include <cstdint>

namespace research_acceleration { struct ContractConfig; }

namespace slaaps
{

constexpr unsigned kCanards = 4;
constexpr unsigned kAxes = 3;
constexpr unsigned kThrustPoints = 5;
constexpr unsigned kScheduleKnots = 2;
constexpr unsigned kWrenchAxes = 2 * kAxes;
constexpr unsigned kWrenchInputs = kCanards + 1;
// BatteryStatus v1 declares three instances. The native observer asserts this
// portable configuration bound against the generated topic definition.
constexpr int32_t kPowerBatteryInstances = 3;

enum class ManualInputSource : int32_t { Unconfigured, RcOnly, MavlinkOnly };
enum class DataAssessment : int32_t { Incomplete = 0, Calibrated = 1, Qualified = 2 };

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
	float force_derivative[kAxes] {}; // Signed N/rad, same condition and deflection convention.
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
	float force_derivative[kAxes][kCanards] {}; // Signed N/rad at this same measured corner.
	float trim_force[kAxes] {}; // Total nongravitational N at canard trim and this EDF knot.
	float trim_moment[kAxes] {}; // Total N m about CG at that same reference.
	float edf_force_per_thrust[kAxes] {}; // Installed N per N of the shared EDF curve.
	float edf_moment_per_thrust[kAxes] {}; // Installed N m per N, including offset/reaction torque.
};

struct WrenchConfiguration {
	int32_t enabled{}; // Explicit measured-model declaration; never acceleration admission.
	float trim_force[kAxes] {}; // Fixed condition: total nongravitational force at trim/condition_thrust.
	float trim_moment[kAxes] {}; // Body FRD moment about CG at the same reference.
	float edf_force_per_thrust[kAxes] {}; // Measured installed N/N; not normalized or inferred.
	float edf_moment_per_thrust[kAxes] {}; // Measured installed N m/N, not inferred from CG alone.
	float force_residual_tolerance[kAxes] {}; // Positive per-axis physical N acceptance bounds.
	float moment_residual_tolerance[kAxes] {}; // Positive per-axis physical N m acceptance bounds.
};

// Physical affine wrench W = offset + effectiveness * [C1,C2,C3,C4,T_N].
// Rows are body FRD Fx,Fy,Fz,Mx,My,Mz about CG; canards are absolute normalized
// calibrated commands. The intercept already accounts for canard trim and EDF
// reference thrust. Interpolate these coefficients/intercepts, not reference products.
struct WrenchModel {
	float effectiveness[kWrenchAxes][kWrenchInputs] {};
	float offset[kWrenchAxes] {};
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

// Source selection and optional request-admission policy. Configured limits
// grant no trusted session, qualified clock map or actuator authority.
struct CompanionConfiguration {
	int32_t enabled{};
	int32_t mavlink_instance{-1};
	int32_t source_system{};
	int32_t source_component{};
	int32_t source_id{}; // Exact positive 31-bit identity from the request payload.
	int32_t maximum_age_us{};
	int32_t admission_enabled{};
	int32_t disarmed_policy{}; // 0 absent, 1 reject requests, 2 validate without control eligibility.
	float acceleration_minimum[kAxes] {}; // Inertial acceleration at CG, body FRD, m/s^2.
	float acceleration_maximum[kAxes] {};
	float acceleration_norm_max{};
	int32_t minimum_validity_us{};
	int32_t maximum_validity_us{};
	int32_t maximum_command_age_us{};
	int32_t maximum_future_skew_us{};
	int32_t maximum_clock_uncertainty_us{};
	int32_t maximum_relative_drift_ppm{}; // Bound on |d(validator time)/d(producer time) - 1|.
	int32_t timestamp_uncertainty_us{}; // Declared total timestamp/quantization/translator error allowance.
	int32_t maximum_round_trip_us{};
	int32_t map_interval_us{}; // Maximum frozen producer-domain map span.
};

// Explicit propulsion supply selection. This is a health/voltage constraint,
// never a measured thrust-curve condition or flight admission.
struct PowerConfiguration {
	int32_t battery_instance{-1};
	int32_t battery_id{};
	int32_t battery_source{-1};
	int32_t maximum_age_us{};
	float voltage_min{};
	float voltage_max{};
};

// Optional CG acceleration feedback policy. Native parameter intent is captured
// with the profile; it is not evidence of an applied producer filter or latency.
struct AccelerationFeedbackConfiguration {
	int32_t enabled{};
	int32_t maximum_age_us{};
	int32_t maximum_bias_age_us{};
	int32_t latency_bound_us{}; // Declared relative sensor/filter latency, not qualified automatically.
	int32_t settling_us{}; // Stable-source wait; never extends original sample expiry.
	float maximum_specific_force_m_s2{};
	float maximum_acceleration_m_s2{};
	float gravity_m_s2{};
	float imu_position[kAxes] {}; // Native EKF2_IMU_POS_*: CG to IMU, body FRD. Do not subtract cg.
	float cutoff_hz{}; // Native IMU_ACCEL_CUTOFF intent; zero disables its filter.
};

// Numerical outer acceleration feedback only. Enabling this configuration
// supplies no measured-envelope qualification, session or output permission.
struct AccelerationControllerConfiguration {
	int32_t enabled{};
	float acceleration_feedback_p[kAxes] {};
	float correction_limit[kAxes] {}; // Body-FRD inertial acceleration correction [m/s^2].
};

struct VehicleConfiguration {
	int32_t data_schema_version{1}; // SLAAPS profile schema, not an external record format.
	// Operator-declared external record reference. Parameters do not verify the
	// record's authenticity or assessment and confer no flight/output authority.
	int32_t data_record_id{};
	int32_t data_revision{}; // Revision of the referenced record.
	int32_t data_assessment{}; // DataAssessment selector stored as native INT32.
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
	int32_t manual_ready_wait_s{}; // One bounded disarmed operator window, not an output lease.
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
	PowerConfiguration power{};
	WrenchConfiguration wrench{};
	AccelerationFeedbackConfiguration feedback{};
	AccelerationControllerConfiguration acceleration_control{};
};

enum class ConfigurationUse : uint8_t { Bench, StabilizedManual, EdfCommissioning = 2 };
enum class ConfigurationFault : uint8_t {
	None, ParameterRange, MissingRecord, InvalidInertia, InvalidServo,
	InvalidTiming, InvalidEffectiveness, InvalidController, InvalidThrustCurve,
	Active, NoCandidate, InvalidWrench, InvalidFeedback, InvalidAccelerationControl
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
// The full declared activation-reply window plus first-command budget must fit
// every supporting lease. Failure preserves reserve_us; no deadline is renewed.
bool activationReserveUs(const VehicleConfiguration &configuration, uint64_t &reserve_us);
bool manualActivationTimingConfigured(const VehicleConfiguration &configuration);
bool benchActivationTimingConfigured(const VehicleConfiguration &configuration);

// Enabled-table checks, including fixed moment normalization and servo limits
// at every corner. Disabled tables may be staged. No envelope qualification,
// interpolation, sensor admission or flight permission is supplied here.
ConfigurationResult validateControlSchedule(const VehicleConfiguration &configuration);

// Optional physical model; completeness gates acceleration conversion only.
// Disabled models may be staged; both builders reject disabled models and
// preserve the destination on failure. No measurement/flight qualification.
ConfigurationResult validateWrenchConfiguration(const VehicleConfiguration &configuration);
bool makeWrenchModel(const VehicleConfiguration &configuration, WrenchModel &model);
bool makeWrenchCornerModel(const VehicleConfiguration &configuration, unsigned airspeed_knot,
			   unsigned edf_knot, WrenchModel &model);

// Completeness gates acceleration feedback only. Disabled/incomplete feedback
// does not remove existing Manual/Bench model validity; scalar finite/native
// ranges still apply. Numerical configuration never supplies flight admission.
ConfigurationResult validateAccelerationFeedbackConfiguration(const VehicleConfiguration &configuration);

// Complete scheduled numerical controller only; independent of Manual/Bench
// profile acceptance. Disabled/incomplete values never grant flight authority.
ConfigurationResult validateAccelerationControlConfiguration(const VehicleConfiguration &configuration);

// Independent source-group validation, including when no vehicle model exists.
// Disabled in-range fields may be staged; enabled fields must all be explicit.
ConfigurationResult validateCompanionConfiguration(const VehicleConfiguration &configuration);

// Source-only profiles remain valid with admission disabled. Conversion is
// independent of the vehicle model and preserves destination on failure.
bool companionAdmissionConfigured(const VehicleConfiguration &configuration);
// Conservative declared clock-policy feasibility, not measured timing proof.
// Actual renewal must still pass its final observation/deadline checks.
bool companionClockRenewalConfigured(const CompanionConfiguration &configuration, uint64_t &lead_us,
				     uint64_t *observation_slack_us = nullptr);
bool makeCompanionContractConfiguration(const VehicleConfiguration &configuration,
					research_acceleration::ContractConfig &contract);

// Servo-bench profiles may stage scalar-valid incomplete power fields. EDF
// commissioning and stabilized manual require the complete explicit group.
ConfigurationResult validatePowerConfiguration(const PowerConfiguration &configuration, bool required = false);
ConfigurationResult validatePowerConfiguration(const VehicleConfiguration &configuration, bool required = false);

// Interpolation is valid only at the declared curve voltage/condition.
// Flight admission must establish that condition separately.
bool thrustToCommand(const VehicleConfiguration &configuration, float thrust_n, float &command);
bool commandToThrust(const VehicleConfiguration &configuration, float command, float &thrust_n);

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
