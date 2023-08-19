# shellcheck disable=SC3043,SC2046,SC2086,SC3010,SC2034
SKIPUNZIP=1

unzip -o "$ZIPFILE" -x 'META-INF/*' -d $MODPATH >&2
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm_recursive $MODPATH/system/bin 0 2000 0755 0755
set_perm_recursive $MODPATH/modules 0 2000 0755 0755

. $MODPATH/modules/arsenal.sh

loger() {
	log=$(echo "$*" | tr -s " ")
	true && ui_print "  DEBUG: $log"
}

MOD_BASE=$NVBASE/zcharge

mkdir -p $MOD_BASE

[ ! -f /data/adb/zcharge/zcharge.conf ] &&
	mv $MODPATH/zcharge.conf $MOD_BASE

CONF=/data/adb/zcharge/zcharge.conf

$MODPATH/system/bin/zcharge -ds
$MODPATH/system/bin/zcharge -es
