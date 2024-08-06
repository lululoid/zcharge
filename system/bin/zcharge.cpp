#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h> // Include this header for umask
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <sys/stat.h> // Include this header for umask
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using namespace std;

// Use directives to avoid repeatedly writing std::
using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::ifstream;
using std::ofstream;
using std::stoi;
using std::string;

// Global declarations
string on_switch, off_switch, charging_switch_path;
bool enabled = true; // Global flag to track enabled/disabled state

void loger(const string &log) { cout << "  DEBUG: " << log << endl; }

void loger(const string &log, int value) {
  cout << "  DEBUG: " << log << value << endl;
}

void notif(const string &body) {
  string cmd =
      "su -lp 2000 -c \"cmd notification post -S bigtext -t 'zcharge' 'Tag' '" +
      body + "'\"";
  system(cmd.c_str());
}

// Function to execute an SQL statement
void execute_sql(sqlite3 *db, const string &sql) {
  char *errmsg = 0;
  int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errmsg);
  if (rc != SQLITE_OK) {
    cerr << "SQL error: " << errmsg << endl;
    sqlite3_free(errmsg);
  }
}

// Function to parse the configuration file and insert values into the database
void parse_and_insert_config(sqlite3 *db, const string &config_file) {
  ifstream infile(config_file);
  string line;

  // Prepare the SQL insert statement
  string sql = "INSERT INTO zcharge_config (key, value) VALUES ";

  while (getline(infile, line)) {
    // Ignore comments and empty lines
    if (line.empty() || line[0] == '#')
      continue;

    size_t pos = line.find('=');
    if (pos != string::npos) {
      string key = line.substr(0, pos);
      string value = line.substr(pos + 1);

      // Remove whitespace from key
      key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
      key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));

      // Handle the specific case for charging_switch
      if (key == "charging_switch") {
        std::istringstream iss(value);
        string temp;
        iss >> temp >> on_switch >> off_switch;
        // Insert specific values
        sql += "('charging_switch_path', '" + temp + "'),";
        sql += "('charging_switch_on', '" + on_switch + "'),";
        sql += "('charging_switch_off', '" + off_switch + "'),";
        // Extract and set charging_switch_path
        charging_switch_path = temp;
      } else {
        // Add the key-value pair to the SQL statement
        sql += "('" + key + "', '" + value + "'),";
      }
    }
  }

  // Remove the last comma and add a semicolon
  if (sql.back() == ',') {
    sql.back() = ';';
  } else {
    sql += ';';
  }

  // Execute the SQL statement
  execute_sql(db, sql);
}

// Function to convert a configuration file to a database
void conf_to_db(const string &db_file, const string &config_file) {
  sqlite3 *db;
  int rc = sqlite3_open(db_file.c_str(), &db);
  if (rc) {
    cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
    return;
  } else {
    cout << "Opened database successfully" << endl;
  }

  // Create the table for configuration parameters
  string create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS zcharge_config (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT NOT NULL,
            value TEXT NOT NULL
        );
    )";
  execute_sql(db, create_table_sql);

  // Parse the configuration file and insert values into the database
  parse_and_insert_config(db, config_file);

  // Close the database
  sqlite3_close(db);

  cout << "Configuration inserted into the database successfully" << endl;
}

int read_bat_temp() {
  ifstream file("/sys/class/power_supply/battery/temp");
  int temp;
  file >> temp;
  return temp;
}

int read_capacity() {
  ifstream file("/sys/class/power_supply/battery/capacity");
  int capacity;
  file >> capacity;
  return capacity;
}

string read_charging_state() {
  ifstream file("/sys/class/power_supply/battery/status");
  string status;
  file >> status;
  return status;
}

void switch_off() {
  if (charging_switch_path != off_switch) {
    ofstream file(charging_switch_path);
    file << off_switch;
    loger("Switching off charging");
  }
}

void switch_on() {
  if (charging_switch_path != on_switch) {
    ofstream file(charging_switch_path);
    file << on_switch;
    loger("Switching on charging");
  }
}

string get_value_from_db(sqlite3 *db, const string &key) {
  string value;
  string sql = "SELECT value FROM zcharge_config WHERE key='" + key + "';";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    }
  }

  sqlite3_finalize(stmt);
  return value;
}

void get_charging_switch_path(sqlite3 *db) {
  charging_switch_path = get_value_from_db(db, "charging_switch_path");
}

string get_value_from_charging_switch(const string &path) {
  ifstream file(path);
  if (!file.is_open()) {
    cerr << "Failed to open file: " << path << endl;
    return "";
  }

  string value;
  file >> value;
  return value;
}

void limiter_service(const string &db_file) {
  sqlite3 *db;
  int rc = sqlite3_open(db_file.c_str(), &db);
  if (rc) {
    cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
    return;
  }

  int recharging_limit = 0, capacity_limit = 0, temp_limit = 0;
  string sql = "SELECT key, value FROM zcharge_config";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      string key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
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
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  loger("enabled: ", enabled);
  loger("recharging_limit: ", recharging_limit);
  loger("capacity_limit: ", capacity_limit);
  loger("temperature_limit: ", temp_limit);
  loger("on_switch: " + on_switch);
  loger("off_switch: " + off_switch);
  loger("charging_switch_path: " + charging_switch_path);

  while (enabled) {
    int capacity = read_capacity();
    string charging_state = read_charging_state();

    if (charging_state == "Charging") {
      string charging_switch_value =
          get_value_from_charging_switch(charging_switch_path);
      if (capacity >= capacity_limit) {
        if (charging_switch_value != off_switch) {
          switch_off();
        }
      }

      if (read_bat_temp() >= temp_limit) {
        switch_off();
        while (read_bat_temp() > temp_limit - 10) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (capacity < capacity_limit) {
          switch_on();
        }
      }

      if (capacity <= recharging_limit) {
        string charging_switch_value =
            get_value_from_charging_switch(charging_switch_path);
        if (charging_switch_value != on_switch) {
          switch_on();
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void update_config(sqlite3 *db, const string &key, const string &value) {
  string sql = "INSERT OR REPLACE INTO zcharge_config (key, value) VALUES ('" +
               key + "', '" + value + "');";
  execute_sql(db, sql);
}

void enable_zcharge(const string &db_file) {
  sqlite3 *db;
  int rc = sqlite3_open(db_file.c_str(), &db);
  if (rc) {
    cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
    return;
  }

  update_config(db, "enabled", "1");

  sqlite3_close(db);
  cout << "zcharge enabled" << endl;
}

void disable_zcharge(const string &db_file) {
  sqlite3 *db;
  int rc = sqlite3_open(db_file.c_str(), &db);
  if (rc) {
    cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
    return;
  }

  update_config(db, "enabled", "0");

  sqlite3_close(db);
  cout << "zcharge disabled" << endl;
}

int main(int argc, char *argv[]) {
  const string default_db_file = "/data/adb/zcharge/zcharge.db";

  if (argc == 4 && string(argv[1]) == "--convert") {
    string old_config = argv[2];
    string new_config = argv[3];

    // Call conversion function here
    conf_to_db(new_config, old_config);
  } else if (argc == 3 && string(argv[1]) == "--enable") {
    string db_file = argv[2];
    enable_zcharge(db_file);
  } else if (argc == 3 && string(argv[1]) == "--disable") {
    string db_file = argv[2];
    disable_zcharge(db_file);
  } else {
    // Use the default database file if none is provided
    string db_file = default_db_file;

    // Daemonize the process
    pid_t pid, sid;

    // Fork the parent process
    pid = fork();
    if (pid < 0) {
      exit(EXIT_FAILURE);
    }

    // If we got a good PID, then we can exit the parent process
    if (pid > 0) {
      exit(EXIT_SUCCESS);
    }

    // Change the file mode mask
    umask(0);

    // Open any logs here
    // ...

    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0) {
      exit(EXIT_FAILURE);
    }

    // Change the current working directory
    if ((chdir("/")) < 0) {
      exit(EXIT_FAILURE);
    }

    // Close out the standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Start the service thread
    std::thread service_thread(limiter_service, db_file);

    // Wait for the service thread to complete
    service_thread.join();
  }

  return 0;
}
