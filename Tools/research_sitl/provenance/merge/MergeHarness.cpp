#include "ReceiptMerge.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>

using namespace research_sitl::provenance;

namespace research_sitl::provenance
{
struct MergeTestPeer {
	static bool room(const ReceiptMerge &merge)
	{
		// Throttle only; relaxed loads add no publication/reuse happens-before.
		return merge.lanes_[0].write.load(std::memory_order_relaxed)
		       - merge.lanes_[0].read.load(std::memory_order_relaxed) < QueueCapacity;
	}
	static void occupyWriter(ReceiptMerge &merge) { merge.lanes_[0].writer.test_and_set(); }
	static void exhaustQueueCounter(ReceiptMerge &merge)
	{
		merge.lanes_[0].read.store(std::numeric_limits<uint32_t>::max());
		merge.lanes_[0].write.store(std::numeric_limits<uint32_t>::max());
	}
};
}

namespace
{
void require(bool condition)
{
	if (!condition) { std::fputs("merge contract assertion failed\n", stderr); std::abort(); }
}

Config config(size_t count = 2)
{
	Config c{};
	c.source_count = count;

	for (size_t i = 0; i < SourceCapacity; ++i) { c.sources[i] = {1, 2, 3, i + 4, i + 10, i + 20}; }

	return c;
}

Receipt receipt(const Config &c, size_t slot, uint64_t sequence, uint64_t value = 0)
{
	Receipt r{};
	r.identity = c.sources[slot];
	r.sequence = sequence;
	r.source_time_us = sequence;
	r.kind = sequence == 1 ? Kind::Initial : Kind::Sample;
	r.observation[0] = value;
	return r;
}

void ordering()
{
	const auto c = config();
	auto owner = std::make_unique<ReceiptMerge>(c);

	for (uint64_t i = 1; i <= 3; ++i) {
		auto a = receipt(c, 0, i, i == 2 ? 1 : 0); // A/B/A cannot coalesce
		a.source_time_us = 1000 + i;
		require(owner->publish(0, a));
		require(owner->publish(1, receipt(c, 1, i, i)));
	}

	for (uint64_t i = 0; i < 6; ++i) {
		require(owner->step(10 + i)); // intentionally unrelated source clocks
		Record r{};
		require(owner->copy(i, r) && r.slot == i % 2 && r.owner_order == i + 1);
		require(r.receipt.sequence == i / 2 + 1);

		if (r.slot == 0) { require(r.receipt.observation[0] == (i == 2 ? 1U : 0U)); }
	}

	require(!owner->step(20) && !owner->closed());
	owner->stop(21);
	require(owner->closure().cause == Fault::OwnerStop && owner->count() == 6);
}

void failures()
{
	const auto c = config(1);

	for (int scenario = 0; scenario < 8; ++scenario) {
		auto owner = std::make_unique<ReceiptMerge>(c);
		require(owner->publish(0, receipt(c, 0, 1)) && owner->step(10));
		auto next = receipt(c, 0, 2);
		Fault expected = Fault::None;

		switch (scenario) {
		case 0: ++next.identity.lifetime; expected = Fault::IdentityMismatch; break;

		case 1: next.sequence = 3; expected = Fault::Sequence; break;

		case 2: next.kind = Kind::Initial; expected = Fault::RepeatedInitial; break;

		case 3: next.source_time_us = 0; expected = Fault::SourceTime; break;

		case 4: next.kind = Kind::Lost; expected = Fault::SourceLost; break;

		case 5: next.kind = static_cast<Kind>(255); expected = Fault::InvalidKind; break;

		case 6: ++next.identity.clock; expected = Fault::IdentityMismatch; break;

		case 7: expected = Fault::OwnerTime; break;
		}

		require(owner->publish(0, next));
		require(!owner->step(scenario == 7 ? 9 : 11));
		require(owner->closed() && owner->count() == 1 && owner->closure().cause == expected);
		require(!owner->publish(0, receipt(c, 0, 3)));
		owner->stop(99);
		require(owner->closure().cause == expected && !owner->step(100));
	}

	auto owner = std::make_unique<ReceiptMerge>(c);
	require(owner->publish(0, receipt(c, 0, 2)) && !owner->step(1));
	require(owner->closure().cause == Fault::MissingInitial);

	for (int scenario = 0; scenario < 3; ++scenario) {
		owner = std::make_unique<ReceiptMerge>(c);
		const Fault expected = scenario == 0 ? Fault::QueueOverflow :
				       (scenario == 1 ? Fault::ConcurrentProducer : Fault::CounterExhausted);

		if (scenario == 0) {
			for (uint64_t i = 1; i <= QueueCapacity; ++i) { require(owner->publish(0, receipt(c, 0, i))); }

		} else if (scenario == 1) { MergeTestPeer::occupyWriter(*owner); }

		else { MergeTestPeer::exhaustQueueCounter(*owner); }

		require(!owner->publish(0, receipt(c, 0, QueueCapacity + 1)));
		require(!owner->step(1) && owner->closed() && owner->count() == 0);
		require(owner->closure().cause == expected); // fault wins over queued Initial
	}

	owner = std::make_unique<ReceiptMerge>(c);

	for (uint64_t i = 1; i <= ArchiveCapacity; ++i) {
		require(owner->publish(0, receipt(c, 0, i)) && owner->step(i));
	}

	require(owner->publish(0, receipt(c, 0, ArchiveCapacity + 1)));
	require(!owner->step(ArchiveCapacity + 1));
	require(owner->closure().cause == Fault::EvidenceExhausted && owner->closure().has_attempted);
	require(owner->count() == ArchiveCapacity);
}

void concurrentHandoff()
{
	const auto c = config(1);
	auto owner = std::make_unique<ReceiptMerge>(c);
	constexpr uint64_t Handoffs = 1000;
	std::thread producer([&]() {
		for (uint64_t i = 1; i <= Handoffs; ++i) {
			while (!MergeTestPeer::room(*owner)) { std::this_thread::yield(); }

			auto r = receipt(c, 0, i, i);
			r.observation.fill(i);
			require(owner->publish(0, r));
		}
	});

	for (uint64_t i = 1; i <= Handoffs; ++i) {
		while (!owner->step(i)) { require(!owner->closed()); std::this_thread::yield(); }

		Record r{};
		require(owner->copy(i - 1, r));

		for (uint64_t value : r.receipt.observation) { require(value == i); }

	}

	producer.join();
	owner->stop(Handoffs + 1);
	require(owner->count() == Handoffs && owner->closure().cause == Fault::OwnerStop);
}

uint64_t now()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
					     std::chrono::steady_clock::now().time_since_epoch()).count());
}

int replay(const char *input, const char *output)
{
	const auto c = config(1); // synthetic offline merge identities, never native trust
	auto owner = std::make_unique<ReceiptMerge>(c);
	FILE *source = std::fopen(input, "r");

	if (!source) { return 1; }

	bool ok = true;

	while (ok) {
		unsigned long long fields[9] {};
		const int n = std::fscanf(source, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
					  &fields[0], &fields[1], &fields[2], &fields[3], &fields[4],
					  &fields[5], &fields[6], &fields[7], &fields[8]);

		if (n == EOF) { ok = !std::ferror(source); break; }

		if (n != 9) { ok = false; break; }

		auto r = receipt(c, 0, fields[0]);
		r.source_time_us = fields[1];

		for (size_t i = 0; i < ObservationWords; ++i) { r.observation[i] = fields[i + 2]; }

		ok = owner->publish(0, r) && owner->step(now());
	}

	if (std::fclose(source) != 0) { ok = false; }

	owner->stop(now());

	if (!ok || owner->closure().cause != Fault::OwnerStop || !owner->count()) { return 1; }

	FILE *sink = std::fopen(output, "wx");

	if (!sink) { return 1; }

	for (size_t i = 0; ok && i < owner->count(); ++i) {
		Record r{};
		require(owner->copy(i, r));
		ok = std::fprintf(sink, "%llu %llu %zu %llu %llu", static_cast<unsigned long long>(r.owner_order),
				  static_cast<unsigned long long>(r.owner_time_us), r.slot,
				  static_cast<unsigned long long>(r.receipt.sequence),
				  static_cast<unsigned long long>(r.receipt.source_time_us)) > 0;

		for (uint64_t value : r.receipt.observation) {
			ok = ok && std::fprintf(sink, " %llu", static_cast<unsigned long long>(value)) > 0;
		}

		ok = ok && std::fputc('\n', sink) != EOF;
	}

	ok = ok && std::fprintf(sink, "closed owner_stop %llu %zu\n",
				static_cast<unsigned long long>(owner->closure().owner_time_us), owner->count()) > 0;

	if (std::fclose(sink) != 0) { ok = false; }

	return ok ? 0 : 1;
}
}

int main(int argc, char **argv)
{
	if (argc == 4 && std::strcmp(argv[1], "--replay") == 0) { return replay(argv[2], argv[3]); }

	if (argc != 1) { return 1; }

	ordering(); failures(); concurrentHandoff();
	std::puts("PASS closed merge ordering, terminal faults, capacity and 1000 concurrent handoffs");
	return 0;
}
