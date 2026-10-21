#pragma once

#include "FunctionProviderBase.hpp"

// Values come from the permanently attached MAIN transaction, never a shared
// actuator topic. MixingOutput restricts this provider to MAIN channels 1..4
// in their declared order. Scheduling remains the output owner's obligation.
class FunctionResearchStabilized final : public FunctionProviderBase
{
public:
	explicit FunctionResearchStabilized(ResearchOutputTransaction &transaction) : _transaction(transaction) {}
	static FunctionProviderBase *allocate(const Context &context)
	{
		return context.research_transaction ? new FunctionResearchStabilized(*context.research_transaction) : nullptr;
	}
	void update() override {}
	float value(OutputFunction function) override
	{
		return _transaction.control(static_cast<unsigned>(function) - static_cast<unsigned>(OutputFunction::Research_Stabilized1));
	}
private:
	ResearchOutputTransaction &_transaction;
};
