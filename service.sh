# shellcheck disable=SC2034,SC2086
MODDIR=${0%/*}
export MODDIR
NVBASE=/data/adb
CONF=$NVBASE/zcharge/zcharge.db
MOD_BASE=$NVBASE/zcharge

$MODBIN/zcharge
logcat -v time zcharge:V *:S --file=$MOD_BASE/zcharge.log
