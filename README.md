# zcharge

Simple module to limit charging capacity. This is my experiment coding in C++, jump straight using chatgpt, pretty fun.

## Usage

```
Usage: zcharge [OPTIONS] [ARGS...]
Options:
  --print                                  Print configuration content
  --convert <old_config> <new_config>      Convert the old configuration file to the new database format.
  --enable [config_db]                     Enable zcharge with the specified database file (or default).
  --disable [config_db]                    Disable zcharge with the specified database file (or default).
  --reload                                 Tell service to reload the config.
  --update <key=value> [config_db]         Update the configuration value for the specified key. If [config_db] is omitted, uses default.
  -h, --help                               Show this help message and exit.

Example key-value pairs:
  charging_switch_path=/path/to/switch
  charging_switch_on=1
  charging_switch_off=0
  recharging_limit=75
  capacity_limit=85
  temperature_limit=410
```
