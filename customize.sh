# shellcheck disable=SC3043,SC2046,SC2086,SC3010,SC2034
SKIPUNZIP=1
TEMPDIR=/data/local/tmp/zcharge
export MODPATH
export MODBIN=$MODPATH/system/bin

mkdir $TEMPDIR
# exec 3>&1 2>&1
# set -x # Prints commands, prefixing them with a character stored in an environmental variable ($PS4)
unzip -o "$ZIPFILE" -x 'META-INF/*' -d $MODPATH >&2
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm_recursive $MODBIN 0 2000 0755 0755
set_perm_recursive $MODPATH/modules 0 2000 0755 0755

loger() {
	log=$(echo "$*" | tr -s " ")
	true && ui_print "  DEBUG: $log"
}

compare_num() {
	local version=$1
	local operator=$2
	local version0=$3

	awk -v version="${version}" -v version_prev="${version0}" -v op="${operator}" '
    BEGIN {
        if (op == ">") {
            exit version > version_prev ? 0 : 1
        } else if (op == "<") {
            exit version < version_prev ? 0 : 1
        } else {
            exit 1
        }
    }'
}

MOD_BASE=$NVBASE/zcharge

mkdir -p $MOD_BASE

CONF_NEW=$MODPATH/zcharge.conf
[ ! -f /data/adb/zcharge/zcharge.conf ] &&
	cp $CONF_NEW $MOD_BASE

CONF=$MOD_BASE/zcharge.conf
prev_v=$(sed -n 's/version=//p' $CONF)
current_v=$(sed -n 's/version=//p' $CONF_NEW)

if [ -n "$prev_v" ]; then
	compare_num "$current_v" ">" "$prev_v" && {
		cp $MODPATH/zcharge.conf $MOD_BASE/zcharge.conf.old
		cp $MODPATH/zcharge.conf $MOD_BASE
		ui_print "
> Config updated. Old config is 
$MOD_BASE/zcharge.conf.old"
	}
elif [[ -z $prev_v ]]; then
	cp $MODPATH/zcharge.conf $MOD_BASE
fi

$MODBIN/zcharge -ds
$MODBIN/zcharge -es

cp $MODBIN/zcharge $NVBASE/modules/zcharge/system/bin
cp $MODPATH/modules/arsenal.sh \
	$NVBASE/modules/zcharge/modules
