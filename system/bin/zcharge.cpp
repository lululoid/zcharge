#include "sqlite3.h"
#include <android/log.h>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace std;

#define LOG_TAG "zcharge"
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

string on_switch, off_switch, switch_, charging_switch_path,
    charging_switch_value, charging_state, db_file;
int current_now;
bool enabled, thread_success = false;
volatile sig_atomic_t reload_config = 0;

bool send_reload_signal(const string &zcharge_pid_file) {
  ifstream file(zcharge_pid_file);
  if (!file.is_open()) {
    ALOGW("Failed to open PID file: %s", zcharge_pid_file.c_str());
    return false;
  }

  pid_t pid;
  if (!(file >> pid)) {
    ALOGW("Failed to read PID from file: %s", zcharge_pid_file.c_str());
    file.close();
    return false;
  }
  file.close();

  if (pid <= 0) {
    ALOGW("Invalid PID in file: %s", zcharge_pid_file.c_str());
    return false;
  }

  if (kill(pid, SIGHUP) != 0) {
    ALOGW("Failed to send SIGHUP to process with PID: %d", pid);
    return false;
  }

  return true;
}

void notif(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  string command =
      "su -lp 2000 -c \"command cmd notification post -S bigtext -t "
      "'zcharge' 'zcharge' '" +
      string(buffer) + "'\"";
  int result = system(command.c_str());

  if (result != 0) {
    ALOGE("System command failed: Error code %d", result);
  } else {
    ALOGD("Notification sent: Success code %d", result);
  }
}

void execute_sql(sqlite3 *db, const string &sql) {
  char *errmsg = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
    ALOGE("SQL error: %s", errmsg);
    sqlite3_free(errmsg);
  }
}

void parse_and_insert_config(sqlite3 *db, const string &config_file) {
  ifstream infile(config_file);
  if (!infile.is_open()) {
    ALOGE("Failed to open configuration file: %s", config_file.c_str());
    return;
  }

  string line, sql = "INSERT INTO zcharge_config (key, value) VALUES ";
  while (getline(infile, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t pos = line.find('=');
    if (pos != string::npos) {
      string key = line.substr(0, pos);
      string value = line.substr(pos + 1);
      key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
      key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));

      if (key == "charging_switch") {
        istringstream iss(value);
        string temp;
        if (!(iss >> temp >> on_switch >> off_switch)) {
          ALOGE("Failed to parse charging_switch value: %s", value.c_str());
          continue; // Skip this line if parsing fails
        }
        sql += "('charging_switch_path', '" + temp + "'),";
        sql += "('charging_switch_on', '" + on_switch + "'),";
        sql += "('charging_switch_off', '" + off_switch + "'),";
        charging_switch_path = temp;
      } else {
        sql += "('" + key + "', '" + value + "'),";
      }
    } else {
      ALOGE("Invalid configuration line format: %s", line.c_str());
    }
  }

  // Remove trailing comma and append semicolon
  if (sql.length() > 30) { // Ensure there is something to finalize
    sql.back() = ';';
    execute_sql(db, sql);
  } else {
    ALOGE("No valid SQL statement to execute after parsing configuration file");
  }
}

void conf_to_db(const string &db_file, const string &config_file) {
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }
  ALOGI("Opened database successfully");

  const char *create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS zcharge_config (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT NOT NULL,
            value TEXT NOT NULL
        );
    )";

  char *errmsg = nullptr;
  if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &errmsg) !=
      SQLITE_OK) {
    ALOGE("Failed to create table: %s", errmsg);
    sqlite3_free(errmsg);
    sqlite3_close(db);
    return;
  }

  ALOGI("Table created or verified successfully");

  // Parsing configuration and inserting into the database
  parse_and_insert_config(db, config_file);

  if (sqlite3_close(db) != SQLITE_OK) {
    ALOGE("Failed to close database: %s", sqlite3_errmsg(db));
  } else {
    ALOGI("Database closed successfully");
  }

  ALOGI("Configuration inserted into the database successfully");
}

int read_bat_temp() {
  ifstream file("/sys/class/power_supply/battery/temp");
  if (!file.is_open()) {
    ALOGE("Failed to open temperature file");
    return -1;
  }
  int temp;
  file >> temp;
  return temp;
}

int read_capacity() {
  ifstream file("/sys/class/power_supply/battery/capacity");
  if (!file.is_open()) {
    ALOGE("Failed to open capacity file");
    return -1;
  }
  int capacity;
  file >> capacity;
  return capacity;
}

string read_charging_state() {
  ifstream file("/sys/class/power_supply/battery/status");
  if (!file.is_open()) {
    ALOGE("Failed to open status file");
    return "";
  }
  string status;
  file >> status;
  return status;
}

int read_current_now() {
  string file_path = "/sys/class/power_supply/battery/current_now";
  ifstream file(file_path);
  if (!file.is_open()) {
    ALOGE("Failed to open %s file", file_path.c_str());
    return -1;
  }
  int current_now;
  file >> current_now;
  return current_now;
}

string check_sign(int num) {
  if (num > 0) {
    return "+";
  } else if (num < 0) {
    return "-";
  } else if (num == 0) {
    return "0";
  } else {
    return "";
  }
}

bool is_charging() {
  current_now = read_current_now();
  string sign = check_sign(current_now);
  return (charging_state == "Charging" && charging_switch_value == on_switch &&
          sign == "-");
}

void write_charging_switch(const string &value) {
  ofstream file(charging_switch_path);
  if (!file.is_open()) {
    ALOGE("Failed to open charging switch: %s", charging_switch_path.c_str());
    return;
  }
  file << value;
  ALOGD("Written %s › %s", value.c_str(), charging_switch_path.c_str());
}

void set_charging_switch(const string &switch_) {
  constexpr int wait_time = 20;
  // Write the new value to the charging switch file only if necessary
  if (charging_switch_value != switch_) {
    write_charging_switch(switch_);
    charging_switch_value = switch_; // Update the global value after writing

    bool switch_off = (switch_ == off_switch);
    bool switch_on = (switch_ == on_switch);

    for (int i = 0; i < wait_time; ++i) {
      bool current_status = is_charging();
      if ((switch_off && !current_status && current_now == 0) ||
          (switch_on && current_status && current_now < 0)) {
        ALOGD("Current is %dmA", current_now);
        if (current_status) {
          ALOGI("Charging turned on");
        } else
          ALOGI("Charging turned off");
        return;
      }
      ALOGI("Waiting %d second...", i + 1);
      this_thread::sleep_for(chrono::seconds(1));
    }
    ALOGI("Waited %d seconds, current is %dmA", wait_time, current_now);
  }
}

void signal_handler(int signum) {
  if (signum == SIGHUP) {
    reload_config = 1;
  } else if (signum == SIGTERM) {
    // restore value
    write_charging_switch(on_switch);
    ALOGW("zcharge terminated");
    exit(signum);
  } else {
    ALOGE("Received invalid signal: %d", signum);
  }
}

string get_value_from_db(sqlite3 *db, const string &key) {
  string value;
  string sql = "SELECT value FROM zcharge_config WHERE key='" + key + "';";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    }
  } else {
    ALOGE("Failed to execute query: %s", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  return value;
}

string get_value_from_charging_switch(const string &path) {
  ifstream file(path);
  if (!file.is_open()) {
    ALOGE("Failed to open file: %s", path.c_str());
    return "";
  }
  string value;
  file >> value;
  return value;
}

void limiter_service(const string &db_file) {
  ALOGI("Starting limiter_service");
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }

  // Initial configuration load
  int recharging_limit, capacity_limit, temp_limit, temperature;
  bool plugged = false, notified = false, cooldown = true, cooling_off = false;
  string sql = "SELECT key, value FROM zcharge_config";
  string bc_switch_value;
  sqlite3_stmt *stmt;

  auto load_config = [&]() {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        string key =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        string value =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (key == "recharging_limit")
          recharging_limit = stoi(value);
        else if (key == "capacity_limit")
          capacity_limit = stoi(value);
        else if (key == "temperature_limit")
          temp_limit = stoi(value);
        else if (key == "charging_switch_path")
          charging_switch_path = value;
        else if (key == "charging_switch_on")
          on_switch = value;
        else if (key == "charging_switch_off")
          off_switch = value;
        else if (key == "enabled")
          enabled = (stoi(value) == 1);
      }
      ALOGD("Configuration loaded");
      ALOGD("enabled: %d", enabled);
      ALOGD("recharging_limit: %d%%", recharging_limit);
      ALOGD("capacity_limit: %d%%", capacity_limit);
      ALOGD("temperature_limit: %.1f°C", temp_limit / 10.0);
      ALOGD("on_switch: %s", on_switch.c_str());
      ALOGD("off_switch: %s", off_switch.c_str());
      ALOGD("charging_switch_path: %s", charging_switch_path.c_str());
    } else {
      ALOGE("Failed to execute query: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
  };

  load_config();
  sqlite3_close(db);

  try {
    ALOGD("Entering main loop");
    ALOGD("Current: %dmA", read_current_now());
    while (enabled) {
      if (reload_config) {
        ALOGI("Reloading configuration...");
        reload_config = 0;
        if (sqlite3_open(db_file.c_str(), &db)) {
          ALOGE("Can't reopen database: %s", sqlite3_errmsg(db));
          continue; // Skip this iteration if reopening fails
        }

        load_config();
        sqlite3_close(db);
        continue; // Restart the loop with new config
      }

      int capacity = read_capacity();
      if (capacity == -1) {
        ALOGE("Failed to read capacity");
        this_thread::sleep_for(chrono::seconds(1));
        continue;
      }

      charging_state = read_charging_state();
      if (charging_state.empty()) {
        ALOGE("Failed to read charging state");
        this_thread::sleep_for(chrono::seconds(1));
        continue;
      }

      if (charging_state == "Charging") {
        charging_switch_value =
            get_value_from_charging_switch(charging_switch_path);

        if (!plugged) {
          ALOGI("Charger plugged");
          plugged = true;
        }

        // Charging controller
        if (capacity >= capacity_limit && is_charging()) {
          ALOGI("Capacity limit reached (%d%%)", capacity_limit);
          // Inform user it's okay to remove charging now
          notif("Capacity limit reached(%d%%), stopping charging...", capacity);
          set_charging_switch(
              off_switch); // Cooldown before charging to capacity_limit again
                           // to reduce heat
          cooldown = true;
          ALOGI("Cooldown to %d%% before recharging again", recharging_limit);
        } else if (capacity < recharging_limit && cooldown) {
          ALOGI("Battery level(%d%%) is dropped below recharging limit(%d%%)",
                capacity, recharging_limit);
          set_charging_switch(on_switch);
          cooldown = false;
        }

        // Temperature controller
        temperature = read_bat_temp();
        if (temperature > temp_limit && is_charging()) {
          ALOGI("Temperature(%.1f°C) exceed limit(%.1f°C)", temperature / 10.0,
                temp_limit / 10.0);
          set_charging_switch(off_switch);
          cooling_off = true;
        } else if (temperature < temp_limit && cooling_off) {
          ALOGI("Temperature is back to normal(%.1f°C), turning on charging...",
                temp_limit / 10.0);
          // Restore last charging_switch_value
          set_charging_switch(on_switch);
          cooling_off = false;
        }
      }

      if (charging_state == "Discharging") {
        if (plugged) {
          ALOGI("Charger unplugged");
          plugged = false;
        }

        // Avoid discharging below 30%
        if (!notified && capacity == 30) {
          notif(
              "Battery is %d%%, charge your phone to increase battery lifespan",
              capacity);
          notified = true;
        } else if (notified && capacity != 30) {
          notified = false;
        }
      }

      if (!thread_success) {
        notif("zcharge started successfully.");
        thread_success = true;
      }
      this_thread::sleep_for(chrono::seconds(1));
    }
  } catch (const exception &e) {
    ALOGE("Exception in limiter_service: %s", e.what());
    thread_success = false;
  }
}

sqlite3 *open_database(const string &db_file) {
  sqlite3 *db = nullptr;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return nullptr; // Return nullptr if opening the database fails
  }
  return db; // Return the database connection handle if successful
}

void update_config(sqlite3 *db, const string &key, const string &value) {
  string sql = "UPDATE zcharge_config SET value = '" + value +
               "' WHERE key = '" + key + "';";

  try {
    execute_sql(db, sql);
    ALOGI("Config updated for key: %s with value: %s", key.c_str(),
          value.c_str());
  } catch (const exception &e) {
    ALOGE("Failed to update config for key: %s with value: %s. Error: %s",
          key.c_str(), value.c_str(), e.what());
    cerr << "Failed to update config for key: " << key.c_str()
         << "with value: " << value.c_str() << "Error: " << e.what() << endl;
  }
}

void print_config(const string &db_file) {
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }

  const char *sql = "SELECT key, value FROM zcharge_config;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    ALOGI("====== Zcharge configuration ======");
    cout << "====== Zcharge configuration ======" << endl;

    // Print each row in "key: value" format
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      string key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      string value =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
      cout << key << ": " << value << endl;
      ALOGD("%s: %s", key.c_str(), value.c_str());
    }
  } else {
    ALOGE("Failed to execute query: %s", sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

void print_usage() {
  cout << "Usage: zcharge [OPTIONS] [ARGS...]" << endl;
  cout << "Options:" << endl;
  cout << "  --print                                  Print configuration "
          "content"
       << endl;
  cout << "  --convert <old_config> <new_config>      Convert the old "
          "configuration file to the new database format."
       << endl;
  cout << "  --enable [config_db]                     Enable zcharge with the "
          "specified database file (or default)."
       << endl;
  cout << "  --disable [config_db]                    Disable zcharge with the "
          "specified database file (or default)."
       << endl;
  cout << "  --reload                                 Tell service to reload "
          "the config."
       << endl;
  cout
      << "  --update <key=value> [config_db]         Update the configuration "
         "value for the specified key. If [config_db] is omitted, uses default."
      << endl;
  cout << "  -h, --help                               Show this help message "
          "and exit."
       << endl;
  cout << endl;
  cout << "Example key-value pairs:" << endl;
  cout << "  charging_switch_path=/path/to/switch" << endl;
  cout << "  charging_switch_on=1" << endl;
  cout << "  charging_switch_off=0" << endl;
  cout << "  recharging_limit=75" << endl;
  cout << "  capacity_limit=85" << endl;
  cout << "  temperature_limit=410" << endl;
}

void save_pid(const string &pid_file) {
  ofstream file(pid_file);
  if (file.is_open()) {
    file << getpid(); // Save the PID of the current process
    file.close();
  } else {
    ALOGE("Failed to open PID file for writing: %s", pid_file.c_str());
  }
}

void daemonize_process(const string &zcharge_pid_file) {
  pid_t pid, sid;
  // Fork the process
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  if (pid > 0) {
    exit(EXIT_SUCCESS); // Parent process exits
  }

  // Set file mode creation mask to 0
  umask(0);

  // Create a new session ID
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  // Change the working directory to root
  if (chdir("/") < 0) {
    exit(EXIT_FAILURE);
  }

  // Close standard file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // Save the PID to a file
  save_pid(zcharge_pid_file);

  // Start the limiter service
  thread service_thread(limiter_service, db_file);
  service_thread.join();
}

bool isProcessZcharge(pid_t pid) {
  // Construct the path to the command line file in /proc/[PID]/cmdline
  char cmdlinePath[PATH_MAX];
  bool zcharge_proc;
  snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%d/cmdline", pid);

  // Open the cmdline file
  ifstream cmdlineFile(cmdlinePath, ios::in | ios::binary);
  if (!cmdlineFile.is_open()) {
    // If we cannot open the file, the process might not exist or we have no
    // permissions
    return false;
  }

  // Read the contents of the cmdline file
  string cmdline;
  getline(cmdlineFile, cmdline, '\0');

  // Close the file
  cmdlineFile.close();

  // Check if the command line contains "zcharge"
  zcharge_proc = cmdline.find("zcharge") != string::npos;
  ALOGI("zcharge is running with cmdline: %s", cmdline.c_str());
  return zcharge_proc;
}

int main(int argc, char *argv[]) {
  const string default_db_file = "/data/adb/zcharge/zcharge.db";
  const string zcharge_pid_file =
      "/data/adb/zcharge/zcharge.pid"; // Path to the PID file

  // Signal handling
  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);

  // Argument validation
  if (argc > 4) {
    print_usage();
    return EXIT_FAILURE;
  } else if (argc == 4 && string(argv[1]) == "--convert") {
    string old_config = argv[2];
    string new_config = argv[3];

    conf_to_db(new_config, old_config);
    return EXIT_SUCCESS;
  } else if (argc == 2 &&
             (string(argv[1]) == "-h" || string(argv[1]) == "--help")) {
    print_usage();
    return EXIT_SUCCESS;
  } else if (argc == 2 && string(argv[1]) == "--reload") {
    send_reload_signal(zcharge_pid_file);
    return EXIT_SUCCESS;
  } else if (argc == 2 && string(argv[1]) == "--print") {
    print_config(default_db_file);
    return EXIT_SUCCESS;
  } else if ((argc == 3 || argc == 2) && string(argv[1]) == "--enable") {
    string db_file = (argc == 3) ? argv[2] : default_db_file;
    sqlite3 *db = open_database(db_file);

    update_config(db, "enabled", "1");
    sqlite3_close(db);
    ALOGD("zcharge enabled");
    return EXIT_SUCCESS;
  } else if ((argc == 3 || argc == 2) && string(argv[1]) == "--disable") {
    string db_file = (argc == 3) ? argv[2] : default_db_file;
    sqlite3 *db = open_database(db_file);

    update_config(db, "enabled", "0");
    sqlite3_close(db);
    ALOGI("zcharge disabled");
    return EXIT_SUCCESS;
  } else if ((argc == 3 || argc == 4) && string(argv[1]) == "--update") {
    string db_file = (argc == 4) ? argv[1] : default_db_file;
    string key_value = (argc == 3) ? argv[2] : argv[3];
    size_t pos = key_value.find('=');

    if (pos == string::npos) {
      ALOGE("Invalid key-value pair: %s", key_value.c_str());
      return EXIT_FAILURE;
    }

    string key = key_value.substr(0, pos);
    string value = key_value.substr(pos + 1);
    sqlite3 *db = open_database(db_file);

    update_config(db, key, value);
    sqlite3_close(db);
    return EXIT_SUCCESS;
  }

  db_file = (argc == 2) ? argv[1] : default_db_file;
  // Start limiter_service if theres none
  ifstream pidFile(zcharge_pid_file);

  if (!pidFile.is_open()) {
    cerr << "Error: Could not open PID file." << endl;
    ALOGE("Error: Could not open PID file.");
  }

  pid_t pid;
  pidFile >> pid; // Read the PID from the file

  if (pidFile.fail()) {
    cerr << "Error: Invalid PID in file." << endl;
    ALOGE("Error: Invalid PID in file.");
  }
  pidFile.close();

  if (isProcessZcharge(pid)) {
    ALOGI("zcharge PID: %d", pid);
    cout << "zcharge PID: " << pid << endl;
    return EXIT_SUCCESS;
  }
  daemonize_process(zcharge_pid_file);

  if (!thread_success) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
