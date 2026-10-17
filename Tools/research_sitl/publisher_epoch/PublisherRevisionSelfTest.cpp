#include "PublisherRevisionSubscription.hpp"
#include <uORB/topics/orb_test.h>
#include <px4_platform_common/log.h>

namespace research_stabilized_epoch
{

int publisherRevisionSelfTest()
{
	// orb_test is a disposable test topic, never an estimator or actuator input.
	orb_test_s message{};
	message.timestamp = 1;
	message.val = 11;
	orb_advert_t publisher = orb_advertise(ORB_ID(orb_test), &message);
	PublisherRevisionSubscription subscription{ORB_ID(orb_test)};
	orb_test_s copied{};
	unsigned initial{}, continuous{}, restarted{};
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool success, const char *name) {
		++checks;

		if (!success) { ++failures; PX4_ERR("revision selftest: %s", name); }
	};
	check(publisher != nullptr && subscription.copyRevision(&copied, initial) && initial != 0 && copied.val == 11,
	      "initial publication");
	const unsigned initial_generation = subscription.get_last_generation();
	message.timestamp = 2;
	message.val = 22;
	check(orb_publish(ORB_ID(orb_test), publisher, &message) == PX4_OK
	      && subscription.copyRevision(&copied, continuous) && continuous == initial
	      && subscription.get_last_generation() > initial_generation && copied.val == 22,
	      "continuous data does not change lifecycle revision");
	check(orb_unadvertise(publisher) == PX4_OK && !subscription.copyRevision(&copied, restarted) && restarted == 0,
	      "withdrawal rejects cached state");
	const orb_advert_t replacement = orb_advertise(ORB_ID(orb_test), nullptr);
	check(replacement == publisher && subscription.advertised(), "same node re-advertised");
	check(!subscription.copyRevision(&copied, restarted) && restarted == 0,
	      "re-advertisement without new data rejects prior-revision cache");
	message.timestamp = 3;
	message.val = 33;
	check(orb_publish(ORB_ID(orb_test), replacement, &message) == PX4_OK
	      && subscription.copyRevision(&copied, restarted) && restarted > initial && copied.val == 33,
	      "new-revision payload becomes readable");
	unsigned before{}, after{};
	const unsigned last_generation = subscription.get_last_generation();
	check(subscription.cycleAdvertisement(message, before, after) && before == restarted && after > before,
	      "withdrawal and re-advertisement between observations");
	check(subscription.copy(&copied) && subscription.advertised()
	      && subscription.get_last_generation() > last_generation && copied.timestamp == message.timestamp,
	      "ordinary subscription observes advertised monotonic data after hidden cycle");
	check(subscription.copyRevision(&copied, restarted) && restarted == after,
	      "bound copy observes hidden lifecycle change");
	PublisherRevisionBinding binding;
	unsigned revisions[PublisherRevisionBinding::kTopicCount] {initial, initial, initial, initial};
	check(binding.observe(revisions) == 1 && binding.observe(revisions) == 1, "stable tuple keeps core epoch");
	revisions[0] = restarted;
	check(binding.observe(revisions) == 2, "changed tuple advances core epoch");
	revisions[1] = 0;
	check(binding.observe(revisions) == 0, "unavailable revision rejected");
	// An additional instance-zero advertiser also changes the revision. Until it
	// publishes, a bound copy must not attribute existing data to that revision.
	const unsigned overlap_before = subscription.revision();
	const orb_advert_t overlap = orb_advertise(ORB_ID(orb_test), nullptr);
	check(overlap == publisher && subscription.revision() > overlap_before
	      && !subscription.copyRevision(&copied, restarted), "additional advertisement invalidates cached binding");
	check(orb_publish(ORB_ID(orb_test), overlap, &message) == PX4_OK
	      && subscription.copyRevision(&copied, restarted), "additional advertisement can publish new data");

	if (publisher) { orb_unadvertise(publisher); }

	PX4_INFO("publisher_revision_selftest checks=%u failures=%u", checks, failures);
	return failures == 0 ? PX4_OK : PX4_ERROR;
}

} // namespace research_stabilized_epoch
