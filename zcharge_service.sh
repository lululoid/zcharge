#!/system/bin/sh
exec 3>&1 1>>"/data/local/tmp/zcharge.log" 2>&1
set -x # Prints commands, prefixing them with a character stored in an environmental variable ($PS4)

. $MODPATH/modules/arsenal.sh

$MODPATH/system/bin/zcharge -ds
limiter_service
