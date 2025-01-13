#include "my_serial.hpp"

#include <iostream>  
#include <fstream>   
#include <sstream>   
#include <string>   
#include <vector>
#include <iomanip>
#include <random>
#ifdef _WIN32
#include <windows.h> 
#else
#include <sys/time.h> 
#include <ctime>      
#endif

const int MAX_TIME_DEFAULT = 24 * 60 * 60;
const int MAX_TIME_HOUR = 30 * 24 * 60 * 60;
const int MAX_TIME_DAY = 365 * 24 * 60 * 60;
const int HOUR = 60 * 60;
const int DAY = 24 * 60 * 60;
const double TIME_DELAY = 10.0;

template<class T> std::string to_string(const T& v)
{
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

// Время в формате "YYYY-MM-DD HH:MM:SS.MS"
std::string getCurrentTime() {
    std::ostringstream oss;

#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    oss << st.wYear << "-" << st.wMonth << "-" << st.wDay << " "
        << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "."
        << st.wMilliseconds;
#else
    struct timeval tv;
    struct tm* tm;
    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);
    oss << tm->tm_year + 1900 << "-" << tm->tm_mon + 1 << "-" << tm->tm_mday << " "
        << tm->tm_hour << ":" << tm->tm_min << ":" << tm->tm_sec << "."
        << tv.tv_usec / 1000;
#endif

    return oss.str();
}

// Парсинг строки времени в структуру tm
bool parseTime(const std::string& time_str, std::tm& tm) {
    if (time_str.size() < 19) return false;

    std::istringstream ss(time_str);
    char delimiter;
    ss >> tm.tm_year >> delimiter >> tm.tm_mon >> delimiter >> tm.tm_mday
       >> tm.tm_hour >> delimiter >> tm.tm_min >> delimiter >> tm.tm_sec;

    if (ss.fail()) return false;

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    return true;
}

// Функция очистки лог-файла от старых записей (старше max_age_seconds)
void cleanOldEntries(const std::string& logfile_name, int max_age_seconds) {
    std::ifstream logfile(logfile_name);
    if (!logfile.is_open()) {
        std::cerr << "Failed to open log file for cleaning!" << std::endl;
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    std::time_t now = std::time(nullptr);

    while (std::getline(logfile, line)) {
        std::tm tm = {};
        if (parseTime(line.substr(0, 19), tm)) {
            std::time_t entry_time = std::mktime(&tm);
            if (now - entry_time < max_age_seconds) {
                lines.push_back(line);
            }
        }
    }

    logfile.close();

    std::ofstream outfile(logfile_name);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open log file for writing!" << std::endl;
        return;
    }

    for (const auto& l : lines) {
        outfile << l << std::endl;
    }
}

void writeToLog(const std::string& message, std::string log_file_name) {
    std::ofstream logFile(log_file_name, std::ios::app);
    if (logFile.is_open()) {
        std::string currentTime = getCurrentTime();
        logFile << currentTime + ": " + message << std::endl;
        logFile.close();
    } else {
        std::cerr << "Failed to open log file." << std::endl;
    }
}

// Функция для вычисления среднего значения температуры за последние max_age_seconds
double calculateAverageTemperature(const std::string& logfile_name, int max_age_seconds) {
    std::ifstream logfile(logfile_name);
    if (!logfile.is_open()) {
        std::cerr << "Failed to open log file for reading!" << std::endl;
        return 0.0;
    }

    std::vector<double> temperatures;
    std::string line;
    std::time_t now = std::time(nullptr);

    while (std::getline(logfile, line)) {
        std::tm tm = {};
        if (parseTime(line.substr(0, 19), tm)) {
            std::time_t entry_time = std::mktime(&tm);
            if (now - entry_time < max_age_seconds) {
                size_t colon_pos = line.find_last_of(":");
                if (colon_pos != std::string::npos) {
                    std::string temp_str = line.substr(colon_pos + 1);
                    // Удаляем лишние пробелы
                    temp_str.erase(0, temp_str.find_first_not_of(' ')); 
                    temp_str.erase(temp_str.find_last_not_of(' ') + 1);

                    // Пробуем преобразовать строку в число
                    try {
                        double temp = std::stod(temp_str);
                        temperatures.push_back(temp);
                    } catch (const std::invalid_argument& e) {
                        std::cerr << "Failed to parse temperature: " << temp_str << " (invalid argument)" << std::endl;
                    } catch (const std::out_of_range& e) {
                        std::cerr << "Failed to parse temperature: " << temp_str << " (out of range)" << std::endl;
                    }
                }
            }
        }
    }

    if (temperatures.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double temp : temperatures) {
        sum += temp;
    }

    return sum / temperatures.size();
}

// Функция для проверки, содержит ли строка нулевые байты
bool containsNullBytes(const std::string& str) {
    return str.find('\x00') != std::string::npos;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    cplib::SerialPort smport(std::string(argv[1]), cplib::SerialPort::BAUDRATE_115200);
    if (!smport.IsOpen()) {
        std::cout << "Failed to open port '" << argv[1] << "'! Terminating..." << std::endl;
        return -2;
    }

    std::string mystr;
    std::string avg_hour_str;
    std::string avg_day_str;
    
    smport.SetTimeout(TIME_DELAY);

    int counter_avg_hour = 0;
    int counter_avg_day = 0;

    for (;;) {
        smport >> mystr;
        if (!mystr.empty() && !containsNullBytes(mystr)) {
            // Валидация данных: проверяем, что строка содержит только цифры, точку и минус
            bool is_valid = true;
            for (char ch : mystr) {
                if (!isdigit(ch) && ch != '.' && ch != '-') {
                    is_valid = false;
                    break;
                }
            }
            if (is_valid){
                std::cout << "Got: " <<  mystr << std::endl;
                writeToLog(mystr, "log_temp.log");
            }
            cleanOldEntries("log_temp.log", MAX_TIME_DEFAULT);
        } else {
            std::cout << "Got" << " nothing" << std::endl;
        }

        counter_avg_hour++;
        counter_avg_day++;

        // Каждый час вычисляем среднее значение температуры за последний час секунд
        if (counter_avg_hour >= HOUR){
            avg_hour_str = to_string(calculateAverageTemperature("log_temp.log", HOUR));
            writeToLog(avg_hour_str, "log_avg_temp_hour.log");
            counter_avg_hour = 0;
            cleanOldEntries("log_avg_temp_hour.log", MAX_TIME_HOUR);
        }
        

        // Каждые 24 часа вычисляем среднее значение температуры за последний день
        if (counter_avg_day >= DAY){
            avg_day_str = to_string(calculateAverageTemperature("log_temp.log", DAY));
            writeToLog(avg_day_str, "log_avg_temp_day.log");
            counter_avg_day = 0;
            cleanOldEntries("log_avg_temp_day.log", MAX_TIME_DAY);
        }
        
    }

    return 0;
}