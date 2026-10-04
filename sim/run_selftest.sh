#!/usr/bin/env bash
cd "$(dirname "$0")"
g++ -std=gnu++17 -O0 -g -w -I../.pio/libdeps/cores3/ArduinoJson/src -I../include selftest_flow.cpp ../src/core/flowdoc.cpp ../src/core/webhooks.cpp ../src/core/util.cpp -o /tmp/selftest_flow && /tmp/selftest_flow
