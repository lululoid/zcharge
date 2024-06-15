# shellcheck disable=SC3043,SC2046,SC2086,SC3010
loger() {
	log=$(echo "$*" | tr -s " ")
	true && ui_print "  DEBUG: $log"
}

notif() {
	local body=$1

	[ $not = on ] &&
		su -lp 2000 -c "cmd notification post -S bigtext -t 'zcharge' 'Tag' '$body'"
}

read_bat_temp() {
	cat /sys/class/power_supply/battery/temp
}

read_capacity() {
	cat /sys/class/power_supply/battery/capacity
}

read_charging_state() {
	cat /sys/class/power_supply/battery/status
}

limiter_service() {
	local capacity
	local recharging_limit
	local capacity_limit
	local charging_state

	recharging_limit=$(sed -n 's/recharging_limit = //p' $CONF)
	capacity_limit=$(sed -n 's/capacity_limit = //p' $CONF)
	temp_limit=$(sed -n 's/temperature_limit = //p' $CONF)

	while true; do
		capacity=$(read_capacity)
		charging_state=$(read_charging_state)

		if [[ $charging_state = Charging ]]; then
			[ $capacity -ge $capacity_limit ] && {
				sleep 30
				switch_off
			}
		fi

		if [ $(read_bat_temp) -ge $temp_limit ]; then
			temp=$(read_bat_temp | sed 's/\(.\)\(.\)$/\1,\2/')

			switch_off
			until
				[ $(read_bat_temp) -le $((temp_limit - 10)) ]
			do
				sleep 5
			done

			if [ $capacity -lt $capacity_limit ]; then
				switch_on
				temp=$(read_bat_temp | sed 's/\(.\)\(.\)$/\1,\2/')
			fi
		fi

		if [ $capacity -le $recharging_limit ]; then
			switch_on
		fi

		sleep 5
	done &

	resetprop zcharge.service.pid $! && {
		kill -0 $(resetprop zcharge.service.pid) &&
			echo "
> zcharge service activated"
	} || echo "
> zcharge service failed"
}
