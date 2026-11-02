"""
Touches src/io/sensors.cpp before every build so it is always recompiled
and its __DATE__/__TIME__ are the current build time. sensors::begin() sets
an unset (or behind) MCP7940 RTC to that time, so the clock is right after
flashing.
"""
import os

Import("env")

os.utime(os.path.join(env.subst("$PROJECT_DIR"), "src", "io", "sensors.cpp"), None)
