#!/bin/sh

t="$(date +'%Y-%m-%d %H:%M:%S')"
echo "$t: $0 argc: $# args: $@" |tee -a /tmp/trigger.log
printenv |sort |tee -a /tmp/trigger.log
echo "---" |tee -a /tmp/trigger.log
exit 0
