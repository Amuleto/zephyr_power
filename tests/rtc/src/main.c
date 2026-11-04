#include <zephyr/ztest.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <errno.h>

static const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(rtc0));

ZTEST(rtc_gd32, test_calendar_and_tick)
{
	zassert_true(device_is_ready(dev), "RTC initialization failed");
	struct rtc_time initial = {0};
	zassert_equal(rtc_get_time(dev, &initial), -ENODATA,
		      "RTC must require setting after boot");
	struct rtc_time value = {
		.tm_year = 124, .tm_mon = 1, .tm_mday = 29,
		.tm_hour = 23, .tm_min = 59, .tm_sec = 59,
	};
	zassert_ok(rtc_set_time(dev, &value));
	struct rtc_time readback = {0};
	zassert_ok(rtc_get_time(dev, &readback));
	int64_t start = timeutil_timegm64(rtc_time_to_tm(&value));
	int64_t before = timeutil_timegm64(rtc_time_to_tm(&readback));
	zassert_true(before >= start && before <= start + 1);
	k_msleep(2100);
	zassert_ok(rtc_get_time(dev, &readback));
	int64_t after = timeutil_timegm64(rtc_time_to_tm(&readback));
	zassert_true(after >= before + 1 && after <= before + 3,
		     "RTC tick rate incorrect");
	zassert_equal(readback.tm_mon, 2, "Leap-day rollover failed");
	zassert_equal(readback.tm_mday, 1);

	/* Invalid writes must leave the running clock unchanged. */
	value.tm_year = 123;
	zassert_equal(rtc_set_time(dev, &value), -EINVAL);
	value.tm_year = 124;
	value.tm_mon = 3;
	value.tm_mday = 31;
	zassert_equal(rtc_set_time(dev, &value), -EINVAL);
	zassert_ok(rtc_get_time(dev, &readback));
	zassert_equal(readback.tm_mon, 2);
	zassert_equal(readback.tm_mday, 1);

	value.tm_year = 199;
	value.tm_mon = 11;
	value.tm_mday = 31;
	value.tm_hour = 0;
	value.tm_min = 0;
	value.tm_sec = 0;
	zassert_ok(rtc_set_time(dev, &value));
	zassert_ok(rtc_get_time(dev, &readback));
	zassert_equal(readback.tm_year, 199, "Calendar must work past 2038");
	value.tm_year = 200;
	zassert_equal(rtc_set_time(dev, &value), -EINVAL);
}

ZTEST_SUITE(rtc_gd32, NULL, NULL, NULL, NULL, NULL);
