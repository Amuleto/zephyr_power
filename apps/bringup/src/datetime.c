#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static const struct device *const rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc0));
static K_MUTEX_DEFINE(clock_lock);

static int parse_datetime(const char *date, const char *time, struct rtc_time *value)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;
	int max_day;
	char extra;

	/* The trailing conversion rejects unexpected characters after each field. */
	if (sscanf(date, "%d-%d-%d%c", &year, &month, &day, &extra) != 3 ||
	    sscanf(time, "%d:%d:%d%c", &hour, &minute, &second, &extra) != 3 ||
	    year < 2000 || year > 2099 || month < 1 || month > 12 ||
	    hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
	    second < 0 || second > 59) {
		return -EINVAL;
	}

	/* All years supported by this driver fall within 2000-2099. */
	max_day = days[month - 1] + (month == 2 && year % 4 == 0);
	if (day < 1 || day > max_day) {
		return -ERANGE;
	}

	value->tm_year = year - 1900;
	value->tm_mon = month - 1;
	value->tm_mday = day;
	value->tm_hour = hour;
	value->tm_min = minute;
	value->tm_sec = second;

	return 0;
}

static int set_datetime(const struct shell *sh, const char *date, const char *time)
{
	struct rtc_time value = {0};
	int err;

	err = parse_datetime(date, time, &value);
	if (err == -ERANGE) {
		shell_error(sh, "Invalid calendar date");
		return -EINVAL;
	}
	if (err) {
		shell_error(sh, "Use: date YYYY-MM-DD HH:MM:SS (UTC, 2000-2099)");
		return err;
	}

	k_mutex_lock(&clock_lock, K_FOREVER);
	err = rtc_set_time(rtc_dev, &value);
	k_mutex_unlock(&clock_lock);
	if (err) {
		shell_error(sh, "RTC write failed: %d", err);
	}

	return err;
}

static int cmd_date(const struct shell *sh, size_t argc, char **argv)
{
	struct rtc_time current = {0};
	int err;

	if (!device_is_ready(rtc_dev)) {
		shell_error(sh, "RTC device not ready");
		return -ENODEV;
	}

	if (argc == 3) {
		err = set_datetime(sh, argv[1], argv[2]);
		if (err) {
			return err;
		}
	} else if (argc != 1) {
		shell_error(sh, "Use: date [YYYY-MM-DD HH:MM:SS]");
		return -EINVAL;
	}

	/* HXTAL does not keep time on VBAT alone; require setting after boot. */
	k_mutex_lock(&clock_lock, K_FOREVER);
	err = rtc_get_time(rtc_dev, &current);
	k_mutex_unlock(&clock_lock);
	if (err) {
		shell_error(sh, "RTC read failed: %d (set date after boot)", err);
		return err;
	}

	shell_print(sh, "%04d-%02d-%02d %02d:%02d:%02d UTC",
		    current.tm_year + 1900, current.tm_mon + 1, current.tm_mday,
		    current.tm_hour, current.tm_min, current.tm_sec);
	return 0;
}

SHELL_CMD_ARG_REGISTER(date, NULL, "UTC RTC time: date [YYYY-MM-DD HH:MM:SS]",
		       cmd_date, 1, 2);
