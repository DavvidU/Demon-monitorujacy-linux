#! /bin/bash

dd if=/dev/urandom of=~/KatalogDoMonitorowania/10M.txt bs=1M count=10 | dd if=/dev/urandom of=~/KatalogDoMonitorowania/20M.txt bs=1M count=20 | dd if=/dev/urandom of=~/KatalogDoMonitorowania/50M.txt bs=1M count=50 | dd if=/dev/urandom of=~/KatalogDoMonitorowania/100M.txt bs=1M count=100 | dd if=/dev/urandom of=~/KatalogDoMonitorowania/500M.txt bs=1M count=500 | dd if=/dev/urandom of=~/KatalogDoMonitorowania/1000M.txt bs=1M count=1000
