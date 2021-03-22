#!/bin/sh

chmod 444 $(find /sys/class/powercap/intel-rapl:0/ -name "*energy_uj")
