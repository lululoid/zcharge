#include <android/log.h>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace std;

#define LOG_TAG "zcharge"
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

string on_switch, off_switch, charging_switch_path, charging_switch_value;
bool enabled, charging;
mutex mtx;
volatile sig_atomic_t reload_config = 0;

bool send_reload_signal(const std::string &zcharge_pid) {
  std::ifstream file(zcharge_pid);
  if (!file.is_open()) {
    ALOGE("Failed to open PID file: %s", zcharge_pid.c_str());
    return false;
  }

  pid_t pid;
  file >> pid;
  file.close();

  if (pid <= 0) {
    ALOGE("Invalid PID in file: %s", zcharge_pid.c_str());
    return false;
  }

  return kill(pid, SIGHUP) == 0;
}

void signal_handler(int signum) {
  if (signum == SIGHUP) {
    reload_config = 1;
  } else if (signum == SIGTERM) {
    ALOGE("zcharge terminated");
    exit(signum);
  }
}

void notif(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  system(("su -lp 2000 -c \"cmd notification post -S bigtext -t 'zcharge' "
          "'Tag' '" +
          string(buffer) + "'\"")
             .c_str());
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
        iss >> temp >> on_switch >> off_switch;
        sql += "('charging_switch_path', '" + temp + "'),";
        sql += "('charging_switch_on', '" + on_switch + "'),";
        sql += "('charging_switch_off', '" + off_switch + "'),";
        charging_switch_path = temp;
      } else {
        sql += "('" + key + "', '" + value + "'),";
      }
    }
  }
  sql.back() = ';';
  execute_sql(db, sql);
}

void conf_to_db(const string &db_file, const string &config_file) {
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }
  ALOGD("Opened database successfully");
  execute_sql(db, R"(
        CREATE TABLE IF NOT EXISTS zcharge_config (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT NOT NULL,
            value TEXT NOT NULL
        );
    )");
  parse_and_insert_config(db, config_file);
  sqlite3_close(db);
  ALOGD("Configuration inserted into the database successfully");
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
  ifstream file("/sys/class/power_supply/battery/current_now");
  if (!file.is_open()) {
    ALOGE("Failed to open status file");
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
  } else {
    return "0";
  }
}

void switch_off() {
  lock_guard<mutex> lock(mtx);
  int current_now;
  int wait_time = 20;
  if (charging_switch_value != off_switch) {
    ofstream file(charging_switch_path);

    if (!file.is_open()) {
      ALOGE("Failed to open charging switch path file");
      return;
    }
    file << off_switch;
  }

  // Somebody explain why above code is not executed
  // when I ise one if
  if (charging_switch_value != off_switch) {
    ALOGD("Charging switch value: %s", charging_switch_value.c_str());
    ALOGD("Switching off charging");
    for (int i = 0; i < wait_time; ++i) {
      current_now = read_current_now();
      if (check_sign(current_now) == "0") {
        charging = false;
        ALOGD("Current is %dmA", current_now);
        return;
      }
      ALOGD("Waiting %d second...", i);
      this_thread::sleep_for(chrono::seconds(1));
    }

    ALOGD("Waited %d seconds, current is %dmA", wait_time, current_now);
  }
}

void switch_on() {
  lock_guard<mutex> lock(mtx);
  int current_now;
  int wait_time = 20;
  if (charging_switch_value != on_switch) {
    ofstream file(charging_switch_path);

    if (!file.is_open()) {
      ALOGE("Failed to open charging switch path file");
      return;
    }
    file << on_switch;
  }

  if (charging_switch_value != on_switch) {
    ALOGD("Charging switch value: %s", charging_switch_value.c_str());
    ALOGD("Switching on charging");

    for (int i = 0; i < wait_time; ++i) {
      current_now = read_current_now();
      if (check_sign(current_now) == "-") {
        charging = true;
        ALOGD("Current is %dmA", current_now);
        return;
      }
      ALOGD("Waiting %d second...", i);
      this_thread::sleep_for(chrono::seconds(1));
    }
    ALOGD("Waited %d seconds, current is %dmA", wait_time, current_now);
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

string get_charging_switch_path(sqlite3 *db) {
  charging_switch_path = get_value_from_db(db, "charging_switch_path");
  return charging_switch_path;
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
  ALOGD("Starting limiter_service");
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }
  // Initial configuration load
  int recharging_limit = 75, capacity_limit = 85, temp_limit = 410;
  bool charging = false;
  string sql = "SELECT key, value FROM zcharge_config";
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
          enabled = (value == "1");
      }
      ALOGD("Configuration loaded");
      ALOGD("enabled: %d", enabled);
      ALOGD("recharging_limit: %d%%", recharging_limit);
      ALOGD("capacity_limit: %d%%", capacity_limit);
      ALOGD("temperature_limit: %.1f°C", temp_limit / 10.0);
      ALOGD("on_switch: %s", on_switch.c_str());
      ALOGD("off_switch: %s", off_switch.c_str());
      ALOGD("charging_switch_path: %s", charging_switch_path.c_str());
      ALOGD("Entering main loop");
      ALOGD("Current: %dmA", read_current_now());

    } else {
      ALOGE("Failed to execute query: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
  };

  load_config();
  sqlite3_close(db);

  while (true) {
    if (reload_config) {
      ALOGD("Reloading configuration...");
      reload_config = 0;
      sqlite3_open(db_file.c_str(), &db);
      load_config();
      sqlite3_close(db);
      continue; // Restart the loop with new config
    }

    if (!enabled) {
      this_thread::sleep_for(chrono::seconds(1));
      continue;
    }

    int capacity = read_capacity();
    if (capacity == -1) {
      ALOGD("Failed to read capacity");
      this_thread::sleep_for(chrono::seconds(1));
      continue;
    }

    string charging_state = read_charging_state();
    if (charging_state.empty()) {
      ALOGD("Failed to read charging state");
      this_thread::sleep_for(chrono::seconds(1));
      continue;
    }

    if (charging_state == "Charging") {
      charging_switch_value =
          get_value_from_charging_switch(charging_switch_path);
      if (!charging) {
        ALOGD("Charger plugged");
        charging = true;
      }
      if (capacity >= capacity_limit) {
        switch_off();
      }
      if (read_bat_temp() >= temp_limit) {
        switch_off();
        while (read_bat_temp() > temp_limit - 10) {
          this_thread::sleep_for(chrono::seconds(1));
        }
        if (capacity < capacity_limit) {
          switch_on();
        }
      }
      if (capacity <= recharging_limit) {
        switch_on();
      }
    }

    if (charging_state == "Discharging") {
      if (charging) {
        ALOGD("Charger unplugged");
        charging = false;
      }
      if (capacity == 30) {
        notif("Battery is %d%%, charge your phone to increase battery lifespan",
              capacity);
      }
      while (read_charging_state() != "Charging") {
        this_thread::sleep_for(chrono::seconds(1));
      }
    }
    this_thread::sleep_for(chrono::seconds(1));
  }
}

void update_config(sqlite3 *db, const string &key, const string &value) {
  string sql = "UPDATE zcharge_config SET value = '" + value +
               "' WHERE key = '" + key + "';";
  execute_sql(db, sql);
}

void enable_zcharge(const string &db_file) {
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }
  update_config(db, "enabled", "1");
  sqlite3_close(db);
  ALOGD("zcharge enabled");
}

void disable_zcharge(const string &db_file) {
  sqlite3 *db;
  if (sqlite3_open(db_file.c_str(), &db)) {
    ALOGE("Can't open database: %s", sqlite3_errmsg(db));
    return;
  }
  update_config(db, "enabled", "0");
  sqlite3_close(db);
  ALOGD("zcharge disabled");
}

void print_usage() {
  cout << "Usage: zcharge [OPTIONS] [ARGS...]" << endl;
  cout << "Options:" << endl;
  cout << "  --convert <old_config> <new_config>   Convert the old "
          "configuration file to the new database format."
       << endl;
  cout << "  --enable [db_file]                     Enable zcharge with the "
          "specified database file (or default)."
       << endl;
  cout << "  --disable [db_file]                    Disable zcharge with the "
          "specified database file (or default)."
       << endl;
  cout << "  -h, --help                             Show this help message and "
          "exit."
       << endl;
}

void save_pid(const std::string &zcharge_pid) {
  std::ofstream file(zcharge_pid);
  if (file.is_open()) {
    file << getpid(); // Save the PID of the current process
    file.close();
  } else {
    ALOGE("Failed to open PID file for writing: %s", zcharge_pid.c_str());
  }
}

int main(int argc, char *argv[]) {
  const std::string default_db_file = "/data/adb/zcharge/zcharge.db";
  const std::string zcharge_pid =
      "/data/adb/zcharge/zcharge.pid"; // Path to the PID file

  // Signal handling
  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);

  // Argument validation
  if (argc > 4) {
    print_usage();
    return EXIT_FAILURE;
  } else if (argc == 4 && std::string(argv[1]) == "--convert") {
    std::string old_config = argv[2];
    std::string new_config = argv[3];
    conf_to_db(new_config, old_config);
    return 0;
  } else if (argc == 2 && (std::string(argv[1]) == "-h" ||
                           std::string(argv[1]) == "--help")) {
    print_usage();
    return 0;
  } else if (argc == 2 && std::string(argv[1]) == "--reload") {
    send_reload_signal(zcharge_pid);
    return 0;
  } else if ((argc == 3 || argc == 2) && std::string(argv[1]) == "--enable") {
    std::string db_file = (argc == 3) ? argv[2] : default_db_file;
    enable_zcharge(db_file);
    return 0;
  } else if ((argc == 3 || argc == 2) && std::string(argv[1]) == "--disable") {
    std::string db_file = (argc == 3) ? argv[2] : default_db_file;
    disable_zcharge(db_file);
    return 0;
  }

  // Daemonize the process
  pid_t pid, sid;
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  if (pid > 0) {
    exit(EXIT_SUCCESS); // Parent exits
  }

  umask(0); // Set file mode creation mask to 0

  sid = setsid(); // Create a new session ID
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  if (chdir("/") < 0) { // Change working directory
    exit(EXIT_FAILURE);
  }

  // Save the PID to a file
  save_pid(zcharge_pid);

  // Start limiter_service
  std::string db_file = (argc == 2) ? argv[1] : default_db_file;
  std::thread service_thread(limiter_service, db_file);
  service_thread.join();

  return 0;
}
