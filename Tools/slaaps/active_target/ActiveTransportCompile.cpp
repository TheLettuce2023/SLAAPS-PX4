#include <lib/slaaps/ManualWorkerTransport.hpp>
#include <lib/slaaps/ManualBankTransport.hpp>

template class slaaps::ManualBankTransport<0>;
template class slaaps::ManualBankTransport<1>;

// Unlinked target-compiler probe. No instance is started or given permission.
extern "C" {
	extern const uint32_t slaaps_target_sizes[4] = {sizeof(slaaps::EdfOwner), sizeof(slaaps::ManualWorkerTransport),
							sizeof(slaaps::ManualBankTransport<0>), sizeof(slaaps::ManualBankTransport<1>)
						       };
	slaaps::ManualBankTransport<0> *slaaps_target_main_create() { return new slaaps::ManualBankTransport<0>(); }
	slaaps::ManualBankTransport<1> *slaaps_target_aux_create() { return new slaaps::ManualBankTransport<1>(); }
	void slaaps_target_main_destroy(slaaps::ManualBankTransport<0> *bank) { delete bank; }
	void slaaps_target_aux_destroy(slaaps::ManualBankTransport<1> *bank) { delete bank; }
	slaaps::ManualWorkerTransport *slaaps_target_create(slaaps::EdfOwner &owner)
	{
		return new slaaps::ManualWorkerTransport(owner);
	}
	void slaaps_target_destroy(slaaps::ManualWorkerTransport *worker) { delete worker; }
	bool slaaps_target_advertise(slaaps::ManualWorkerTransport &worker) { return worker.advertise(); }
	slaaps::ManualWorkerStatus slaaps_target_begin(slaaps::ManualWorkerTransport &worker, uint64_t now,
			const slaaps::VehicleConfiguration &configuration, const slaaps::EdfPermission &permission,
			const research_stabilized_manual::ManualSample &manual, const research_stabilized_manual::StateSample &state,
			const slaaps::ManualOutputBinding &binding, unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		return worker.begin(now, configuration, permission, manual, state, binding, command_revision, ack_revisions);
	}
	slaaps::ManualWorkerStatus slaaps_target_cycle(slaaps::ManualWorkerTransport &worker, uint64_t now,
			const slaaps::EdfPermission &permission, const research_stabilized_manual::ManualSample &manual,
			const research_stabilized_manual::StateSample &state, const slaaps::ManualOutputBinding &binding)
	{
		return worker.cycle(now, permission, manual, state, binding);
	}
	slaaps::ManualWorkerStatus slaaps_target_selected_begin(slaaps::ManualWorkerTransport &worker, uint64_t now,
			const slaaps::VehicleConfiguration &configuration, const slaaps::EdfPermission &permission,
			const slaaps::SelectedManualInput::Snapshot &snapshot, const slaaps::SelectedInputPolicy &policy,
			const slaaps::ManualOutputBinding &binding, unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		return worker.begin(now, configuration, permission, snapshot, policy, binding, command_revision, ack_revisions);
	}
	slaaps::ManualWorkerStatus slaaps_target_selected_cycle(slaaps::ManualWorkerTransport &worker, uint64_t now,
			const slaaps::EdfPermission &permission, const slaaps::SelectedManualInput::Snapshot &snapshot,
			const slaaps::ManualOutputBinding &binding)
	{
		return worker.cycle(now, permission, snapshot, binding);
	}

	void slaaps_target_stop(slaaps::ManualWorkerTransport &worker) { worker.stop(); }
}
