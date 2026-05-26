/*
 * Device MQTT client: publishes test-pattern telemetry on dev/<id>/telemetry,
 * subscribes to dev/<id>/cmd, and replies on dev/<id>/resp.
 */
#ifndef MQTT_APP_H
#define MQTT_APP_H

#include "net.h"

/* Start the MQTT client task (call after net_start()). */
void mqtt_app_start( const struct app_cfg * cfg );

#endif /* MQTT_APP_H */
