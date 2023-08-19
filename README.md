# zcharge
Simple module to limit charging capacity.

# config
```
# zcharge configuration
enabled = 1 # to determine whether the service is active or inactive
wait = 0 # determine if the service is waiting battery to reached recharging limit
capacity_limit = 80
recharging_limit = 70 # determine when to start recharging again
charging_reminder = 30
# charging switch could be different depending on your device
charging_switch = /sys/devices/platform/soc/4a88000.i2c/i2c-1/1-0055/power_supply/battery/battery_charging_enabled 1 0
```
