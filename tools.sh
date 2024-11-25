# shellcheck disable=SC3043,SC2046,SC2086,SC3010
TAG=zcharge
NVBASE=/data/adb
LOG_DIR=$NVBASE/$TAG
LOG=$LOG_DIR/$TAG.log
CONF=$LOG_DIR/zcharge.db
MODBIN=$MODPATH/system/bin
[ -z $MODPATH ] && MODPATH=$MODDIR

loger() {
	local log=$2

	[ "$#" -eq 2 ] && {
		echo "$log"
		log -t $TAG -p $1 "$log"
		return 0
	}
	return 1
}

start_zcharge_logcat() {
	kill $(pgrep -f 'logcat.*zcharge')
	# shellcheck disable=SC2035
	nohup logcat -v time zcharge:V *:S >>$LOG 2>&1 &
}
