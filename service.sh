# shellcheck disable=SC2034,SC2086
MODDIR=${0%/*}

. $MODPATH/tools.sh

exec 3>&1 1>>"$LOG" 2>&1
set -x # Prints commands, prefixing them with a character stored in an environmental variable ($PS4)
echo "
⟩ $(date -Is)" >>$LOG

zcharge
start_zcharge_logcat
