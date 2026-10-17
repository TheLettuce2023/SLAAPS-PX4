#if !defined(RESEARCH_STABILIZED_SITL_ONLY) || !defined(__PX4_POSIX)
#error This adapter is private to the synthetic POSIX SITL fixture.
#endif

#include "SyntheticConfiguration.hpp"
#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <lib/research_actuator_command/Px4ManualControlInput.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_angular_velocity.h>
#include <uORB/topics/sensor_selection.h>
#include <uORB/topics/estimator_status.h>
#include <uORB/topics/estimator_status_flags.h>
#include <parameters/param.h>
#include <drivers/drv_hrt.h>
#include <gz/transport/Node.hh>
#include <gz/msgs/double_v.pb.h>
#include <cstdio>
#include <cstring>

using namespace research_stabilized_manual;
namespace fixture = research_stabilized_fixture;
// EKF2::PublishStatusFlags refreshes at ~1 Hz. Add this fixture's 100 ms
// scheduling/state budget; fast attitude/rate samples retain their own limits.
constexpr uint64_t kMaximumStatusAgeUs = 1100000;
constexpr uint64_t kMaximumPermissionAgeUs = 600000; // Commander publishes at 2 Hz plus fixture budget.

// Only Run mutates the owner. No stock setpoint/actuator topic is published.
// Estimator instance zero is required by the isolated single-EKF profile.
class StabilizedManualSITL final : public ModuleBase, public px4::ScheduledWorkItem
{
public:
	static Descriptor desc;
	StabilizedManualSITL() : ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::test1) {}
	~StabilizedManualSITL() override
	{
		if (_log) { fclose(_log); }

		if (_events) { fclose(_events); }
	}
	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);
	int print_status() override
	{
		PX4_INFO("state=%d ready=%d fault=%d; synthetic SITL only", _state.load(), _ready.load(), _fault.load());
		return 0;
	}
private:
	void Run() override;
	static bool profileValid();
	bool publish(uint64_t now, bool enabled, const Report &report);
	void revoke(uint64_t now, Status reason);
	void event(uint64_t now, const char *action, Status reason);
	void log(uint64_t now, const StateSample &sample, const ManualSample &manual, const Report &report);
	uORB::Subscription _manual{ORB_ID(manual_control_setpoint)};
	uORB::Subscription _attitude{ORB_ID(vehicle_attitude)};
	uORB::Subscription _rates{ORB_ID(vehicle_angular_velocity)};
	uORB::Subscription _selection{ORB_ID(sensor_selection)};
	uORB::Subscription _estimator{ORB_ID(estimator_status)};
	uORB::Subscription _flags{ORB_ID(estimator_status_flags)};
	uORB::Subscription _armed{ORB_ID(actuator_armed)};
	StabilizedManualControl _owner{};
	gz::transport::Node _node{};
	gz::transport::Node::Publisher _publisher{};
	FILE *_log{};
	FILE *_events{};
	px4::atomic<int> _request{0};
	px4::atomic<int> _state{0};
	px4::atomic<int> _ready{0};
	px4::atomic<int> _fault{0};
	uint64_t _session{};
	uint32_t _gyro_id{};
	uint32_t _accel_id{};
	uint64_t _estimator_time{};
	uint64_t _last_cycle_time{};
	bool _revoked{};
};

ModuleBase::Descriptor StabilizedManualSITL::desc{task_spawn, custom_command, print_usage};

bool StabilizedManualSITL::publish(uint64_t now, bool enabled, const Report &report)
{
	gz::msgs::Double_V message;
	message.add_data(now);
	message.add_data(_session);
	message.add_data(fixture::kLeaseUs);
	message.add_data(enabled ? 1.0 : 0.0);
	message.add_data(enabled ? report.fixed_simulated_thrust_n : 0.0F);

	for (float control : report.allocation.command) { message.add_data(enabled ? control : 0.0F); }

	return _publisher.Publish(message);
}

void StabilizedManualSITL::revoke(uint64_t now, Status reason)
{
	_owner.disable();
	_revoked = true;
	_ready.store(0);
	_state.store(static_cast<int>(State::Fault));
	_fault.store(static_cast<int>(reason));

	if (_session) { (void)publish(now, false, {}); }

	event(now, "revoke", reason);

	PX4_WARN("revoked reason=%d time=%llu", static_cast<int>(reason), (unsigned long long)now);
}

void StabilizedManualSITL::event(uint64_t now, const char *action, Status reason)
{
	fprintf(_events, "%llu,%llu,%s,%d,%d\n", (unsigned long long)now,
		(unsigned long long)_session, action, _state.load(), static_cast<int>(reason));
}

void StabilizedManualSITL::log(uint64_t now, const StateSample &sample, const ManualSample &manual, const Report &report)
{
	fprintf(_log, "%llu,%d,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u",
		(unsigned long long)now, static_cast<int>(_owner.state()),
		(unsigned long long)sample.attitude_timestamp_us, (unsigned long long)sample.attitude_sample_us,
		(unsigned long long)sample.rate_timestamp_us, (unsigned long long)sample.rate_sample_us,
		_gyro_id, _accel_id, sample.attitude_reset_counter, manual.source_system_id, manual.source_component_id);
	fprintf(_log, ",%llu,%llu,%u,%u", (unsigned long long)manual.timestamp_us,
		(unsigned long long)manual.timestamp_sample_us, manual.mavlink_instance, (unsigned)manual.valid);

	for (unsigned i = 0; i < 3; ++i) { fprintf(_log, ",%.9g", (double)manual.axes[i]); }

	for (unsigned i = 0; i < 4; ++i) { fprintf(_log, ",%.9g", (double)sample.attitude(i)); }

	for (unsigned i = 0; i < 4; ++i) { fprintf(_log, ",%.9g", (double)report.hold_reference(i)); }

	for (unsigned i = 0; i < 3; ++i) {
		fprintf(_log, ",%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u", (double)sample.rate_rad_s(i),
			(double)report.rate_setpoint_rad_s(i), (double)report.requested_moment(i),
			(double)report.allocation.achieved(i), (double)report.integral(i),
			(double)report.bounded_moment(i), (double)report.allocation.residual(i),
			(unsigned)report.allocation.saturation_positive(i), (unsigned)report.allocation.saturation_negative(i));
	}

	for (float control : report.allocation.command) { fprintf(_log, ",%.9g", (double)control); }

	fprintf(_log, ",%.9g\n", (double)report.fixed_simulated_thrust_n);
}

void StabilizedManualSITL::Run()
{
	if (should_exit()) {
		revoke(hrt_absolute_time(), Status::InvalidState);
		ScheduleClear();
		exit_and_cleanup(desc);
		return;
	}

	const int request = _request.fetch_and(0);

	if (request == 2) { revoke(hrt_absolute_time(), Status::InvalidState); }

	if (_revoked) { return; }

	// Snapshot every input before reading the evaluation clock (H10 invariant).
	manual_control_setpoint_s manual_message{};
	vehicle_attitude_s attitude{};
	vehicle_angular_velocity_s rates{};
	sensor_selection_s selection{};
	estimator_status_s estimator{};
	estimator_status_flags_s flags{};
	actuator_armed_s armed{};
	const bool have = _manual.copy(&manual_message) && _attitude.copy(&attitude)
			  && _rates.copy(&rates) && _selection.copy(&selection)
			  && _estimator.copy(&estimator) && _flags.copy(&flags) && _armed.copy(&armed);
	const uint64_t now = hrt_absolute_time();
	const auto manual = research_actuator_command::manualControlSampleFromMessage(manual_message);
	const bool valid = have && profileValid() && flags.cs_tilt_align && flags.cs_yaw_align
			   && !flags.fs_bad_hdg && !flags.fs_bad_acc_vertical
			   && selection.gyro_device_id != 0 && selection.accel_device_id != 0
			   && selection.gyro_device_id == estimator.gyro_device_id
			   && selection.accel_device_id == estimator.accel_device_id
			   && estimator.timestamp != 0 && estimator.timestamp <= now
			   && now - estimator.timestamp <= kMaximumStatusAgeUs
			   && flags.timestamp != 0 && flags.timestamp <= now && now - flags.timestamp <= kMaximumStatusAgeUs
			   && armed.timestamp != 0 && armed.timestamp <= now && now - armed.timestamp <= kMaximumPermissionAgeUs
			   && !armed.armed && !armed.kill && !armed.lockdown
			   && !armed.termination && !armed.in_esc_calibration_mode;
	_ready.store(valid && manual.valid);
	StateSample sample{};
	sample.attitude_timestamp_us = attitude.timestamp;
	sample.attitude_sample_us = attitude.timestamp_sample;
	sample.rate_timestamp_us = rates.timestamp;
	sample.rate_sample_us = rates.timestamp_sample;
	sample.attitude = Quaternion(attitude.q);
	sample.rate_rad_s = Vector3f(rates.xyz);
	sample.angular_accel_rad_s2 = Vector3f(rates.xyz_derivative);
	sample.attitude_reset_counter = attitude.quat_reset_counter;
	sample.source_epoch = 1; // One explicit process/session, single EKF and bound IMU IDs below.
	sample.valid = valid;

	if (_session && (selection.gyro_device_id != _gyro_id || selection.accel_device_id != _accel_id
			 || estimator.timestamp < _estimator_time)) { revoke(now, Status::StateChanged); return; }

	_estimator_time = estimator.timestamp;

	if (request == 1) {
		const auto result = _owner.enable(now, manual, sample);

		if (result != Status::Ok) { revoke(now, result); return; }

		_session = now;
		_last_cycle_time = now;
		_gyro_id = selection.gyro_device_id;
		_accel_id = selection.accel_device_id;
		_state.store(static_cast<int>(_owner.state()));
		event(now, "enable", Status::Ok);
		PX4_INFO("enabled synthetic session=%llu gyro=%u", (unsigned long long)_session, _gyro_id);
		return;
	}

	if (!_owner.eligible()) { return; }

	// Interval catch-up can dispatch twice at a frozen lockstep timestamp.
	// This is not a new control timestep: publish nothing and refresh no lease.
	// Every advancing timestamp still goes through the core's strict checks.
	if (now == _last_cycle_time) { event(now, "dispatch_not_due", Status::NotDue); return; }

	Report report{};
	const auto result = _owner.cycle(now, manual, sample, report);

	if (result == Status::NotDue) { return; }

	if (result != Status::Ok) {
		PX4_WARN("cycle=%d manual_valid=%d publication=%llu sample=%llu source=%u:%u instance=%u",
			 static_cast<int>(result), manual.valid, (unsigned long long)manual.timestamp_us,
			 (unsigned long long)manual.timestamp_sample_us, manual.source_system_id,
			 manual.source_component_id, manual.mavlink_instance);
	}

	if (result != Status::Ok) { revoke(now, result); return; }

	if (!publish(now, true, report)) { revoke(now, Status::ArithmeticFailure); return; }

	_last_cycle_time = now;
	_state.store(static_cast<int>(_owner.state()));
	log(now, sample, manual, report);
}

bool StabilizedManualSITL::profileValid()
{
	const char *single_estimator[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE", "COM_RC_IN_MODE"};
	const int32_t expected[] = {0, 0, 1, 1, 1};

	for (unsigned i = 0; i < 5; ++i) {
		int32_t value{};
		const param_t handle = param_find(single_estimator[i]);

		if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != expected[i]) { return false; }
	}

	// No output functions may be assigned in this isolated profile.
	for (const char *prefix : {"SIM_GZ_SV_FUNC", "SIM_GZ_EC_FUNC", "SIM_GZ_WH_FUNC"}) {
		const unsigned count = strcmp(prefix, "SIM_GZ_EC_FUNC") == 0 ? 16 : strcmp(prefix, "SIM_GZ_SV_FUNC") == 0 ? 8 : 4;

		for (unsigned i = 1; i <= count; ++i) {
			char name[24] {};
			snprintf(name, sizeof(name), "%s%u", prefix, i);
			const param_t handle = param_find(name);
			int32_t value{};

			if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != 0) { return false; }
		}
	}
	return true;
}

int StabilizedManualSITL::task_spawn(int argc, char *argv[])
{
	if (argc != 3 || strcmp(argv[1], "slaaps_fixture") != 0) { return print_usage("explicit fixture and CSV path required"); }

	if (!profileValid()) { return print_usage("single EKF, selected MAVLink input and unassigned simulation outputs required"); }

	auto *instance = new StabilizedManualSITL();

	if (!instance) { return PX4_ERROR; }

	if (instance->_owner.configure(fixture::configuration()) != Status::Ok) { delete instance; return PX4_ERROR; }

	instance->_publisher = instance->_node.Advertise<gz::msgs::Double_V>("/model/slaaps_fixture/stabilized_command");
	instance->_log = fopen(argv[2], "w");
	char event_path[512] {};
	const int length = snprintf(event_path, sizeof(event_path), "%s.events.csv", argv[2]);

	if (length < 0 || static_cast<unsigned>(length) >= sizeof(event_path)) { delete instance; return PX4_ERROR; }

	instance->_events = fopen(event_path, "w");

	if (!instance->_publisher || !instance->_log || !instance->_events) { delete instance; return PX4_ERROR; }

	setvbuf(instance->_log, nullptr, _IOLBF, 0);
	setvbuf(instance->_events, nullptr, _IOLBF, 0);

	fprintf(instance->_log,
		"time,state,att_time,att_sample,rate_time,rate_sample,gyro_id,accel_id,reset,sysid,compid,manual_time,manual_sample,instance,manual_valid,roll,pitch,yaw,qw,qx,qy,qz,rw,rx,ry,rz,wx,sx,mx,ax,ix,bx,ex,px,nx,wy,sy,my,ay,iy,by,ey,py,ny,wz,sz,mz,az,iz,bz,ez,pz,nz,u0,u1,u2,u3,thrust\n");
	fprintf(instance->_events, "time,session,action,state,status\n");
	desc.object.store(instance);
	desc.task_id = task_id_is_work_queue;
	instance->_state.store(static_cast<int>(State::Ready));
	instance->event(hrt_absolute_time(), "configure", Status::Ok);
	instance->ScheduleOnInterval(fixture::kStepUs);
	return PX4_OK;
}

int StabilizedManualSITL::custom_command(int argc, char *argv[])
{
	auto *instance = get_instance<StabilizedManualSITL>(desc);

	if (!instance || argc != 1) { return PX4_ERROR; }

	// Private fixture diagnostic: exercise the real work queue at a frozen
	// simulation clock. This grants no output eligibility or new permission.
	if (strcmp(argv[0], "dispatch") == 0) { instance->ScheduleNow(); return PX4_OK; }

	const int request = strcmp(argv[0], "enable") == 0 ? 1 : strcmp(argv[0], "disable") == 0 ? 2 : 0;

	if (!request) { return print_usage("unknown command"); }

	if (request == 2) { instance->_request.store(2); return PX4_OK; }

	if (instance->_state.load() != static_cast<int>(State::Ready)) { return PX4_ERROR; }

	int expected = 0;
	return instance->_request.compare_exchange(&expected, request) ? PX4_OK : PX4_ERROR;
}

int StabilizedManualSITL::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s", reason); }

	PRINT_MODULE_USAGE_NAME("research_stabilized_sitl", "research");
	PRINT_MODULE_USAGE_COMMAND_DESCR("start slaaps_fixture <csv>", "Private synthetic SITL, initially disabled");
	PRINT_MODULE_USAGE_COMMAND_DESCR("enable", "Centered selected manual input and aligned EKF required");
	PRINT_MODULE_USAGE_COMMAND_DESCR("disable", "Revoke; stop/start required for re-entry");
	PRINT_MODULE_USAGE_COMMAND_DESCR("dispatch", "Diagnostic work-queue poke; output permissions unchanged");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return reason ? PX4_ERROR : PX4_OK;
}

extern "C" __EXPORT int research_stabilized_sitl_main(int argc, char *argv[])
{
	return ModuleBase::main(StabilizedManualSITL::desc, argc, argv);
}
