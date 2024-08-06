# shellcheck disable=SC2034,SC2086
MODDIR=${0%/*}
export MODDIR
NVBASE=/data/adb
CONF=$NVBASE/zcharge/zcharge.db
export MODBIN=$MODDIR/system/bin

$MODBIN/zcharge &
