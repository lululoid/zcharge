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

read_bat_temp() {
	cat /sys/class/power_supply/battery/temp
}

read_capacity() {
	cat /sys/class/power_supply/battery/capacity
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
	temp_limit=$(sed -n 's/temperature_limit = //p' $CONF)

	while true; do
		capacity=$(read_capacity)
		charging_state=$(
			cat /sys/class/power_supply/battery/status
		)
		wait=$(sed -n 's/wait = //p' $CONF)

		if [[ $charging_state = Charging ]]; then
			if [ $wait -eq 0 ]; then
				if [ $capacity -ge $capacity_limit ]; then
					sleep 30
					switch_off && set_wait 1
				fi

				[ $capacity -eq $capacity_limit ] &&
					notif \
						"Capacity limit is reached, charging stopped"
			fi

			if [ $(read_bat_temp) -ge $temp_limit ]; then
				temp=$(read_bat_temp | sed 's/\(.\)\(.\)$/\1,\2/')

				switch_off
				notif "Temperature limit reached($temp°C), charging stopped"
				until
					[ $(read_bat_temp) -le $((temp_limit - 10)) ]
				do
					sleep 5
				done

				if [ $capacity -lt $capacity_limit ] &&
					[ $wait -eq 0 ]; then
					switch_on
					temp=$(read_bat_temp | sed 's/\(.\)\(.\)$/\1,\2/')
					notif "Temperature is $temp°C, charging continue"
				fi
			fi
		fi

		if [ $capacity -le $recharging_limit ] &&
			[ $wait -eq 1 ]; then
			switch_on && set_wait 0
		fi

		if [[ $charging_state = Discharging ]]; then
			if [ -z $reminded ] &&
				[ $capacity -eq $charging_reminder ]; then
				sleep 30
				notif \
					"$charging_reminder% left. Please plug your charger." && reminded=true
			fi

			if [ -z $reminded0 ] &&
				[ $capacity -eq $((charging_reminder - 5)) ]; then
				sleep 30
				notif \
					"$capacity% left. This is the last time I remind you for this session" && reminded0=true
			fi
		fi
		sleep 5
	done &

	resetprop zcharge.service.pid $! && {
		kill -0 $(resetprop zcharge.service.pid) &&
			echo "  > zcharge service activated"
	} || echo "  > zcharge service failed"
}
