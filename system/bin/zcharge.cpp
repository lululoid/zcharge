#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <sstream>
#include <string>
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
string on_switch, off_switch, charging_switch;

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
        sql += "('charging_switch_on', '" + on_switch + "'),";
        sql += "('charging_switch_off', '" + off_switch + "'),";
        // Extract and set charging_switch path
        charging_switch = value.substr(0, value.find(on_switch) - 1);
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
  if (charging_switch != off_switch) {
    ofstream file(charging_switch);
    file << off_switch;
    loger("Switching off charging");
  }
}

void switch_on() {
  if (charging_switch != on_switch) {
    ofstream file(charging_switch);
    file << on_switch;
    loger("Switching on charging");
  }
}

void limiter_service(const string &conf) {
  int recharging_limit, capacity_limit, temp_limit;

  ifstream config(conf);
  string line;
  while (getline(config, line)) {
    if (line.find("recharging_limit") != string::npos)
      recharging_limit = stoi(line.substr(line.find('=') + 2));
    else if (line.find("capacity_limit") != string::npos)
      capacity_limit = stoi(line.substr(line.find('=') + 2));
    else if (line.find("temperature_limit") != string::npos)
      temp_limit = stoi(line.substr(line.find('=') + 2));
    else if (line.find("charging_switch") != string::npos) {
      std::istringstream iss(line);
      string key, value;
      iss >> key >> value;
      on_switch = value;
      iss >> value;
      off_switch = value;
      charging_switch = line.substr(line.find('=') + 2);
      charging_switch =
          charging_switch.substr(0, charging_switch.find(on_switch) - 1);
    }
  }

  loger("recharging_limit: ", recharging_limit);
  loger("capacity_limit: ", capacity_limit);
  loger("temperature_limit: ", temp_limit);
  loger("on_switch: " + on_switch);
  loger("off_switch: " + off_switch);
  loger("charging_switch: " + charging_switch);

  while (true) {
    int capacity = read_capacity();
    string charging_state = read_charging_state();

    if (charging_state == "Charging" && capacity >= capacity_limit) {
      std::this_thread::sleep_for(std::chrono::seconds(30));
      switch_off();
    }

    if (read_bat_temp() >= temp_limit) {
      switch_off();
      while (read_bat_temp() > temp_limit - 10) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }

      if (capacity < capacity_limit) {
        switch_on();
      }
    }

    if (capacity <= recharging_limit) {
      switch_on();
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

int main(int argc, char *argv[]) {
  if (argc == 4 && string(argv[1]) == "--convert") {
    string old_config = argv[2];
    string new_config = argv[3];

    // Call conversion function here
    // Convert old_config to new_config
    conf_to_db(new_config, old_config);
  } else {
    string conf = "/data/adb/zcharge/zcharge.conf";
  }

  return 0;
}
