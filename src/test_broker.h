/*
 * Minimal single-client MQTT broker test fixture (see test_broker.c).
 */
#ifndef TEST_BROKER_H
#define TEST_BROKER_H

#include "net.h"

/* Start the test broker (call after net_start()). */
void test_broker_start( const struct app_cfg * cfg );

#endif /* TEST_BROKER_H */
