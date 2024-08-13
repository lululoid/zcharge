#!/system/bin/sh
# shellcheck disable=SC3043,SC2034,SC2086,SC3060,SC3010
SKIPUNZIP=1
unzip -o "$ZIPFILE" -x 'META-INF/*' -d $MODPATH >&2

. $MODPATH/tools.sh
mkdir -p $LOG_DIR

# save full logging
exec 3>&1 1>>$LOG 2>&1
# restore stdout for magisk
exec 1>&3
set -x
echo "
⟩ $(date -Is)" >>$LOG

set_perm_recursive $MODPATH 0 0 0755 0644
set_perm_recursive $MODBIN 0 2000 0755 0755
set_perm_recursive $MODPATH/tools.sh 0 2000 0755 0755
set_perm_recursive $MODPATH/system/bin/zcharge 0 2000 0755 0755

alias zcharge="$MODBIN/zcharge"

[ ! -f $CONF ] &&
	cp $MODPATH/zcharge.db $CONF &&
	loger I "⟩ Configuration is copied to $CONF"

killall $TAG &&
	loger I "
⟩ zcharge terminated"

cp $MODBIN/zcharge $NVBASE/modules/zcharge/system/bin
cp $MODPATH/modules/arsenal.sh $NVBASE/modules/zcharge/modules

su -c zcharge &&
	loger I "
⟩ zcharge started
"
zcharge -h

loger I "
⟩ Starting logcat for zcharge in $LOG
"
start_zcharge_logcat
