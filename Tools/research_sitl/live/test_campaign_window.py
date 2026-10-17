"""No live experiment: test only bounded-window cleanup and GC policy."""

import unittest
from unittest.mock import patch

from campaign_window import campaign_window


class CampaignWindowTests(unittest.TestCase):
    def setup_mocks(self, enabled=True, samples=(100, 200)):
        mocks = {}
        for name, value in (("isenabled", enabled), ("collect", 0), ("disable", None), ("enable", None)):
            context = patch("campaign_window.gc." + name, return_value=value)
            mocks[name] = context.start()
            self.addCleanup(context.stop)
        context = patch("campaign_window.time.monotonic_ns", side_effect=samples)
        mocks["clock"] = context.start()
        self.addCleanup(context.stop)
        return mocks

    def test_normal_window_restores_collection(self):
        mocks = self.setup_mocks()
        with campaign_window():
            mocks["collect"].assert_called_once_with()
            mocks["disable"].assert_called_once_with()
            mocks["enable"].assert_not_called()
        mocks["enable"].assert_called_once_with()

    def test_exception_restores_collection(self):
        mocks = self.setup_mocks()
        with self.assertRaisesRegex(RuntimeError, "injected failure"):
            with campaign_window():
                raise RuntimeError("injected failure")
        mocks["enable"].assert_called_once_with()

    def test_existing_disabled_policy_preserved(self):
        mocks = self.setup_mocks(enabled=False)
        with campaign_window():
            pass
        for name in ("collect", "disable", "enable"):
            mocks[name].assert_not_called()

    def test_window_overrun_fails_and_restores(self):
        mocks = self.setup_mocks(samples=(100, 2_000_000_101))
        with self.assertRaisesRegex(RuntimeError, "two seconds"):
            with campaign_window():
                pass
        mocks["enable"].assert_called_once_with()

    def test_clock_failure_restores(self):
        mocks = self.setup_mocks(samples=(RuntimeError("clock failed"),))
        with self.assertRaisesRegex(RuntimeError, "clock failed"):
            with campaign_window():
                pass
        mocks["enable"].assert_called_once_with()

    def test_clock_regression_fails_and_restores(self):
        mocks = self.setup_mocks(samples=(200, 100))
        with self.assertRaises(RuntimeError):
            with campaign_window():
                pass
        mocks["enable"].assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
