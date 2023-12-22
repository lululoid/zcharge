# shellcheck disable=SC2034,SC2086
MODDIR=${0%/*}
export MODDIR
NVBASE=/data/adb
CONF=$NVBASE/zcharge/zcharge.conf
enabled=$(sed -n 's/enabled = //p' $CONF)
export MODBIN=$MODDIR/system/bin

[ $enabled -eq 1 ] &&
	$MODBIN/zcharge -es
