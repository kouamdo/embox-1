/**
 * @file
 * @brief A simple test for timerfd.
 * @details Sets the timer and calls read() on it once.
 *
 * @date 04.04.17
 * @author Kirill Smirenko
 */

#include <embox/test.h>
#include <kernel/printk.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/timerfd.h>

EMBOX_TEST_SUITE("timerfd suite");

TEST_CASE("simple timerfd test") {
	printk("\n");
	int timeout_sec = 1;

	int error_code;
	int timer_fd = -1;
	struct itimerspec timeout;
	uint64_t expirations_number;

	/* create new timer */
	timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	test_assert_true(timer_fd >= 0);
	printk("Test: timer created.\n");

	/* set timeout */
	timeout.it_value.tv_sec = timeout_sec;
	timeout.it_value.tv_nsec = 0;
	error_code = timerfd_settime(timer_fd, 0, &timeout, NULL);
	test_assert_zero(error_code);
	printk("Test: timer set.\n");

	read(timer_fd, &expirations_number, sizeof(expirations_number)); // blocking
	test_assert(expirations_number == 1);
	close(timer_fd);
}
