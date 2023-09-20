# shellcheck disable=SC3043,SC2046,SC2086,SC3010
loger() {
	log=$(echo "$*" | tr -s " ")
	true && ui_print "  DEBUG: $log"
}

set_wait() {
	sed -i "s/\(wait = \).*/\1$1/" $CONF
}

notif() {
	local body=$1

	su -lp 2000 -c "cmd notification post -S bigtext -t 'zcharge' 'Tag' '$body'"
}

limiter_service() {
	local capacity
	local recharging_limit
	local capacity_limit
	local charging_reminder
	local charging_state
	recharging_limit=$(sed -n 's/recharging_limit = //p' $CONF)
	capacity_limit=$(sed -n 's/capacity_limit = //p' $CONF)
	charging_reminder=$(
		sed -n 's/charging_reminder = //p' $CONF
	)

	while true; do
		capacity=$(cat /sys/class/power_supply/battery/capacity)
		charging_state=$(
			cat /sys/class/power_supply/battery/status
		)
		wait=$(sed -n 's/wait = //p' $CONF)

		[ $wait -eq 0 ] && {
			[ $capacity -ge $capacity_limit ] && {
				sleep 30
				switch_off && set_wait 1
			}
			[ $capacity -eq $capacity_limit ] &&
				notif "Capacity limit is reached. Charging stopped"
		}

		[ $capacity -le $recharging_limit ] && [ $wait -eq 1 ] &&
			switch_on && set_wait 0

		[[ $charging_state = Discharging ]] && {
			[ -z $reminded ] &&
				[ $capacity -eq $charging_reminder ] && {
				sleep 30
				notif \
					"$charging_reminder% left. Please plug your charger." && reminded=true
			}

			[ -z $reminded0 ] &&
				[ $capacity -eq $((charging_reminder - 5)) ] && {
				sleep 30
				notif \
					"$capacity% left. This is the last time I remind you for this session" && reminded0=true
			}
		}
		sleep 2
	done &
	resetprop zcharge.service on
	resetprop zcharge.service.pid $! &&
		echo "  > zcharge service activated"
}
