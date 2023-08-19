# shellcheck disable=SC2034,SC2086
MODDIR=${0%/*}
NVBASE=/data/adb
CONF=$NVBASE/zcharge/zcharge.conf
enabled=$(sed -n 's/enabled = //p' $CONF)

[ $enabled -eq 1 ] &&
	zcharge -es
