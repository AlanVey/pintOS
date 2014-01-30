#ifndef TESTS_DEVICES_TESTS_H
#define TESTS_DEVICES_TESTS_H

void run_test (const char *);

typedef void test_func (void);

extern test_func test_alarm_single;
extern test_func test_alarm_multiple;
extern test_func test_alarm_simultaneous;
extern test_func test_alarm_zero;
extern test_func test_alarm_negative;

void msg (const char *, ...);
void fail (const char *, ...);
void pass (void);

#endif /* tests/devices/tests.h */

