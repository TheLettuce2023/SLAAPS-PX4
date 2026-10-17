#include "FixtureConstants.hpp"
#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <gz/sim/System.hh>
#include <gz/sim/Model.hh>
#include <gz/sim/Link.hh>
#include <gz/plugin/Register.hh>
#include <gz/transport/Node.hh>
#include <gz/msgs/double_v.pb.h>
#include <gz/msgs/imu.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <sdf/Element.hh>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace research_stabilized_fixture
{
// Physical rotational rigid body; only translation is restrained by the SDF
// ball joint. Virtual surface actuators have explicit first-order dynamics.
// This frozen, full-rank synthetic map is not an aerodynamic coefficient model.
class RotationalFixture final : public gz::sim::System,
	public gz::sim::ISystemConfigure, public gz::sim::ISystemPreUpdate,
	public gz::sim::ISystemPostUpdate
{
public:
	~RotationalFixture() override
	{
		for (const auto &topic : _node.SubscribedTopics()) { _node.Unsubscribe(topic); }
	}
	void Configure(const gz::sim::Entity &entity, const std::shared_ptr<const sdf::Element> &sdf,
		       gz::sim::EntityComponentManager &ecm, gz::sim::EventManager &) override
	{
		gz::sim::Model model(entity);
		_link = gz::sim::Link(model.LinkByName(ecm, "base_link"));
		_link.EnableVelocityChecks(ecm);
		_log.open(sdf->Get<std::string>("truth_csv"));
		_magnetic_log.open(sdf->Get<std::string>("truth_csv") + ".mag.csv");
		_magnetic_delay_us = sdf->Get<uint64_t>("magnetic_transport_delay_us");
		_imu_delay_us = sdf->Get<uint64_t>("imu_transport_delay_us");

		if (_magnetic_delay_us > kMaximumSensorDelayUs || _imu_delay_us > kMaximumSensorDelayUs) {
			throw std::runtime_error("Synthetic sensor transport delay exceeds declared bound");
		}

		_magnetic_output = _node.Advertise<gz::msgs::Magnetometer>(
					   "/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/magnetometer_sensor/magnetometer");

		if (!_link.Valid(ecm) || !_log || !_magnetic_log || !_magnetic_output) {
			throw std::runtime_error("Invalid synthetic fixture / CSV / magnetic sensor transport");
		}

		_log << std::setprecision(17);
		_magnetic_log << std::setprecision(17)
			      << "time,flu_x_t,flu_y_t,flu_z_t,packet_x,packet_y,packet_z,delivery_time\n";
		_log << "time,enabled,lease_fault,x,y,z,qw,qx,qy,qz,wx,wy,wz,u0,u1,u2,u3,thrust,impulse_x,impulse_y,impulse_z,command_time,session,lease,t0,t1,t2,t3,a0,a1,a2,a3\n";

		if (!_node.Subscribe("/model/slaaps_fixture/stabilized_command", &RotationalFixture::command, this)
		    || !_node.Subscribe("/model/slaaps_fixture/impulse", &RotationalFixture::impulse, this)
		    || !_node.Subscribe("/model/slaaps_fixture/magnetometer_raw", &RotationalFixture::magneticField, this)) {
			throw std::runtime_error("Synthetic fixture transport subscription failed");
		}

		if (_imu_delay_us > 0) {
			_imu_log.open(sdf->Get<std::string>("truth_csv") + ".imu.csv");
			_imu_output = _node.Advertise<gz::msgs::IMU>(
					      "/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/imu_sensor/imu");

			if (!_imu_log || !_imu_output
			    || !_node.Subscribe("/model/slaaps_fixture/imu_raw", &RotationalFixture::imuRaw, this)) {
				throw std::runtime_error("Synthetic IMU transport setup failed");
			}

			_imu_log << "sample_time,delivery_time,gyro_x,gyro_y,gyro_z,accel_x,accel_y,accel_z\n";
		}
	}

	void PreUpdate(const gz::sim::UpdateInfo &info, gz::sim::EntityComponentManager &ecm) override
	{
		if (info.paused) { return; }

		const auto now = std::chrono::duration_cast<std::chrono::microseconds>(info.simTime).count();
		const double dt = std::chrono::duration<double>(info.dt).count();

		if (now < 0 || !std::isfinite(dt) || dt <= 0 || dt > 0.02) { _lease.revoke(); return; }

		deliverImu(static_cast<uint64_t>(now));
		deliverMagnetic(static_cast<uint64_t>(now));

		gz::math::Vector3d injected;
		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_invalid) { _lease.revoke(); _invalid = false; }

			if (_pending) {
				_lease.update(now, _frame);

				if (!_lease.faulted()) { _accepted = _frame; }

				_pending = false;
			}

			injected = _impulse;
			_impulse.Set(0, 0, 0);
		}

		bool finite_commands = true;

		for (unsigned i = 0; i < 4; ++i) {
			const float raw = _lease.value(now, i); // Polls independent expiry before enabling thrust.
			finite_commands &= std::isfinite(raw);
			const double target = std::isfinite(raw) ? raw : 0.0;
			_target[i] = target;
			_surface[i] += (target - _surface[i]) * (1.0 - std::exp(-dt / kServoTimeConstantS));
		}

		_enabled = !_lease.faulted() && finite_commands;
		const auto pose = _link.WorldPose(ecm);
		const auto velocity = _link.WorldAngularVelocity(ecm);

		if (!pose || !velocity) { return; }

		// Gazebo link FLU -> body FRD. Moment map and damping use FRD throughout.
		const auto flu = pose->Rot().RotateVectorReverse(*velocity);
		const gz::math::Vector3d rate(flu.X(), -flu.Y(), -flu.Z());
		gz::math::Vector3d moment;

		for (unsigned axis = 0; axis < 3; ++axis) {
			for (unsigned servo = 0; servo < 4; ++servo) {
				moment[axis] += kMomentScaleNm * kEffectiveness[axis][servo] * _surface[servo];
			}

			moment[axis] -= kDampingNmS[axis] * rate[axis];
			moment[axis] += injected[axis] / dt;
		}

		_last_impulse = injected;
		const gz::math::Vector3d torque_flu(moment.X(), -moment.Y(), -moment.Z());
		const gz::math::Vector3d force_flu(_enabled ? kFixedThrustN : 0.0, 0, 0);
		_link.AddWorldWrench(ecm, pose->Rot().RotateVector(force_flu), pose->Rot().RotateVector(torque_flu));
	}

	void PostUpdate(const gz::sim::UpdateInfo &info, const gz::sim::EntityComponentManager &ecm) override
	{
		if (info.paused) { return; }

		const auto pose = _link.WorldPose(ecm);
		const auto velocity = _link.WorldAngularVelocity(ecm);

		if (!pose || !velocity) { return; }

		const auto flu = pose->Rot().RotateVectorReverse(*velocity);
		// Same conversion as GZBridge::rotateQuaternion, retained for scoring only.
		const gz::math::Quaterniond enu_to_ned(0, std::sqrt(0.5), std::sqrt(0.5), 0);
		const gz::math::Quaterniond frd_to_flu(0, 1, 0, 0);
		const auto q = enu_to_ned * pose->Rot() * frd_to_flu;
		_log << std::chrono::duration_cast<std::chrono::microseconds>(info.simTime).count()
		     << ',' << _enabled << ',' << _lease.faulted()
		     << ',' << pose->Pos().X() << ',' << pose->Pos().Y() << ',' << pose->Pos().Z()
		     << ',' << q.W() << ',' << q.X() << ',' << q.Y() << ',' << q.Z()
		     << ',' << flu.X() << ',' << -flu.Y() << ',' << -flu.Z();

		for (double surface : _surface) { _log << ',' << surface; }

		_log << ',' << (_enabled ? kFixedThrustN : 0.0)
		     << ',' << _last_impulse.X() << ',' << _last_impulse.Y() << ',' << _last_impulse.Z()
		     << ',' << _accepted.timestamp << ',' << _accepted.session << ',' << _accepted.lease_us;

		for (double target : _target) { _log << ',' << target; }

		for (double surface : _surface) { _log << ',' << surface *kMaximumDeflectionRad; }

		_log << '\n';

		if (++_log_steps % 25 == 0) { _log.flush(); } // Bounded 100 ms log observation latency.
	}

private:
	static constexpr uint64_t kMaximumSensorDelayUs = 100000;
	static constexpr size_t kMaximumQueuedMagneticPackets = 32;
	static constexpr size_t kMaximumQueuedImuPackets = 64;
	struct ImuPacket {
		gz::msgs::IMU packet;
		uint64_t sample_us{};
		uint64_t due_us{};
	};
	void deliverImu(uint64_t now)
	{
		std::deque<ImuPacket> due;
		{
			std::lock_guard<std::mutex> lock(_imu_mutex);

			if (_imu_overflow) { throw std::runtime_error("Synthetic IMU transport queue overflow"); }

			while (!_imu_queue.empty() && _imu_queue.front().due_us <= now) {
				due.push_back(std::move(_imu_queue.front()));
				_imu_queue.pop_front();
			}
		}

		for (const auto &sample : due) {
			if (!_imu_output.Publish(sample.packet)) { throw std::runtime_error("Synthetic IMU delivery failed"); }

			_imu_log << sample.sample_us << ',' << now << ','
				 << sample.packet.angular_velocity().x() << ',' << sample.packet.angular_velocity().y() << ','
				 << sample.packet.angular_velocity().z() << ',' << sample.packet.linear_acceleration().x() << ','
				 << sample.packet.linear_acceleration().y() << ',' << sample.packet.linear_acceleration().z() << '\n';
		}

		if (!due.empty()) { _imu_log.flush(); }
	}
	void imuRaw(const gz::msgs::IMU &packet)
	{
		const uint64_t sample = static_cast<uint64_t>(packet.header().stamp().sec()) * 1000000ULL
					+ static_cast<uint64_t>(packet.header().stamp().nsec()) / 1000ULL;
		std::lock_guard<std::mutex> lock(_imu_mutex);

		if (_imu_queue.size() >= kMaximumQueuedImuPackets) { _imu_overflow = true; return; }

		_imu_queue.push_back({packet, sample, sample + _imu_delay_us});
	}
	struct MagneticPacket {
		gz::msgs::Magnetometer packet;
		gz::math::Vector3d raw;
		uint64_t sample_us{};
		uint64_t due_us{};
	};
	void deliverMagnetic(uint64_t now)
	{
		std::deque<MagneticPacket> due;
		{
			std::lock_guard<std::mutex> lock(_magnetic_mutex);

			if (_magnetic_overflow) { throw std::runtime_error("Synthetic magnetic transport queue overflow"); }

			while (!_magnetic_queue.empty() && _magnetic_queue.front().due_us <= now) {
				due.push_back(std::move(_magnetic_queue.front()));
				_magnetic_queue.pop_front();
			}
		}

		for (const auto &sample : due) {
			if (!_magnetic_output.Publish(sample.packet)) {
				throw std::runtime_error("Synthetic magnetic packet delivery failed");
			}

			std::lock_guard<std::mutex> lock(_magnetic_mutex);
			_magnetic_log << sample.sample_us << ',' << sample.raw.X() << ',' << sample.raw.Y() << ','
				      << sample.raw.Z() << ',' << sample.packet.field_tesla().x() << ','
				      << sample.packet.field_tesla().y() << ',' << sample.packet.field_tesla().z() << ','
				      << now << '\n';
			_magnetic_log.flush();
		}
	}
	void magneticField(const gz::msgs::Magnetometer &message)
	{
		const auto &field = message.field_tesla();
		constexpr double kMaximumFixtureFieldTesla = 1e-4;
		const double norm = std::sqrt(field.x() * field.x() + field.y() * field.y() + field.z() * field.z());

		if (!std::isfinite(norm) || norm < 1e-6 || norm > kMaximumFixtureFieldTesla) { return; }

		// The SDF requests corrected ENU earth field / FLU sensor / tesla units.
		// Preserve the checkout's legacy GZBridge unchanged. Its (-y,-x,z)
		// mapping then yields physical FRD gauss (x,-y,-z)*1e4 from this packet.
		// Only a sensor-protocol conversion; neither estimates nor truth are inputs.
		constexpr double kTeslaToGauss = 1e4;
		gz::msgs::Magnetometer packet = message;
		packet.mutable_field_tesla()->set_x(field.y() * kTeslaToGauss);
		packet.mutable_field_tesla()->set_y(-field.x() * kTeslaToGauss);
		packet.mutable_field_tesla()->set_z(-field.z() * kTeslaToGauss);
		const uint64_t sample = static_cast<uint64_t>(message.header().stamp().sec()) * 1000000ULL
					+ static_cast<uint64_t>(message.header().stamp().nsec()) / 1000ULL;
		std::lock_guard<std::mutex> lock(_magnetic_mutex);

		if (_magnetic_queue.size() >= kMaximumQueuedMagneticPackets) { _magnetic_overflow = true; return; }

		_magnetic_queue.push_back({std::move(packet), {field.x(), field.y(), field.z()}, sample,
					   sample + _magnetic_delay_us});
	}
	static bool integer(double value)
	{
		return std::isfinite(value) && value >= 0 && value <= 9007199254740991.0 && std::floor(value) == value;
	}
	void command(const gz::msgs::Double_V &message)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		bool valid = message.data_size() == 9;

		for (int i = 0; valid && i < 3; ++i) { valid = integer(message.data(i)); }

		valid = valid && (message.data(3) == 0 || message.data(3) == 1)
			&& message.data(4) == (message.data(3) == 1 ? kFixedThrustN : 0.0);

		for (int i = 5; valid && i < 9; ++i) { valid = std::isfinite(message.data(i)) && std::fabs(message.data(i)) <= 1.0; }

		if (!valid) { _invalid = true; return; }

		_frame.timestamp = message.data(0);
		_frame.session = message.data(1);
		_frame.lease_us = message.data(2);
		_frame.enabled = message.data(3) == 1;

		for (unsigned i = 0; i < 4; ++i) { _frame.control[i] = message.data(5 + i); }

		_pending = true;
	}
	void impulse(const gz::msgs::Double_V &message)
	{
		if (message.data_size() != 3) { return; }

		for (double value : message.data()) { if (!std::isfinite(value) || std::fabs(value) > 0.1) { return; } }

		std::lock_guard<std::mutex> lock(_mutex);
		_impulse.Set(message.data(0), message.data(1), message.data(2));
	}
	gz::sim::Link _link{};
	gz::transport::Node _node{};
	gz::transport::Node::Publisher _magnetic_output{};
	gz::transport::Node::Publisher _imu_output{};
	std::ofstream _log{};
	std::ofstream _magnetic_log{};
	std::ofstream _imu_log{};
	std::mutex _mutex{};
	std::mutex _magnetic_mutex{};
	std::mutex _imu_mutex{};
	std::deque<MagneticPacket> _magnetic_queue{};
	std::deque<ImuPacket> _imu_queue{};
	uint64_t _magnetic_delay_us{};
	uint64_t _imu_delay_us{};
	bool _magnetic_overflow{};
	bool _imu_overflow{};
	research_actuator_command::BenchOutputLease _lease{};
	research_actuator_command::BenchOutputFrame _frame{};
	research_actuator_command::BenchOutputFrame _accepted{};
	std::array<double, 4> _surface{};
	std::array<double, 4> _target{};
	unsigned _log_steps{};
	gz::math::Vector3d _impulse{};
	gz::math::Vector3d _last_impulse{};
	bool _pending{};
	bool _invalid{};
	bool _enabled{};
};
} // namespace research_stabilized_fixture

GZ_ADD_PLUGIN(research_stabilized_fixture::RotationalFixture, gz::sim::System,
	      gz::sim::ISystemConfigure, gz::sim::ISystemPreUpdate, gz::sim::ISystemPostUpdate)
