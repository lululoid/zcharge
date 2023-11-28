# shellcheck disable=SC3043,SC2046,SC2086,SC3010,SC2034
SKIPUNZIP=1
TEMPDIR=/data/local/tmp/zcharge
export MODPATH
export MODBIN=$MODPATH/system/bin

mkdir $TEMPDIR
exec 2>"$TEMPDIR/zcharge.log"
set -x # Prints commands, prefixing them with a character stored in an environmental variable ($PS4)
unzip -o "$ZIPFILE" -x 'META-INF/*' -d $MODPATH >&2
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm_recursive $MODBIN 0 2000 0755 0755
set_perm_recursive $MODPATH/modules 0 2000 0755 0755

loger() {
	log=$(echo "$*" | tr -s " ")
	true && ui_print "  DEBUG: $log"
}

MOD_BASE=$NVBASE/zcharge

mkdir -p $MOD_BASE

[ ! -f /data/adb/zcharge/zcharge.conf ] &&
	mv $MODPATH/zcharge.conf $MOD_BASE

CONF=/data/adb/zcharge/zcharge.conf

cp $MODPATH/zcharge.conf $MOD_BASE

$MODBIN/zcharge -ds
$MODBIN/zcharge -es

cp $MODBIN/zcharge $NVBASE/modules/zcharge/system/bin
cp $MODPATH/modules/arsenal.sh $NVBASE/modules/zcharge/modules
