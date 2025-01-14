#include "my_serial.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <ctime>
#include <deque>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Глобальные переменные для хранения логов в памяти
std::mutex log_mutex;
std::deque<std::string> log_temp_memory;          // Основной лог температур
std::deque<std::string> log_avg_temp_hour_memory; // Лог средних значений за час
std::deque<std::string> log_avg_temp_day_memory;  // Лог средних значений за день

// Константы
const int MAX_TIME_DEFAULT = 24 * 60 * 60; // Максимальное время хранения записей в основном логе (24 часа)
const int MAX_TIME_HOUR = 30 * 24 * 60 * 60; // Максимальное время хранения записей в логе за час (30 дней)
const int MAX_TIME_DAY = 365 * 24 * 60 * 60; // Максимальное время хранения записей в логе за день (1 год)
const int HOUR = 60 * 60;                   // Количество секунд в часе
const int DAY = 24 * 60 * 60;               // Количество секунд в дне
const double TIME_DELAY = 10.0;             // Таймаут для чтения данных

// Функция для преобразования любого типа в строку
template<class T>
std::string to_string(const T& v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

// Получение текущего времени в формате "YYYY-MM-DD HH:MM:SS.MS"
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

// Запись в лог (в память)
void writeToLog(const std::string& message, std::deque<std::string>& log_memory) {
    std::lock_guard<std::mutex> lock(log_mutex);
    log_memory.push_back(getCurrentTime() + ": " + message);
}

// Синхронизация лога с диском
void syncLogToDisk(const std::deque<std::string>& log_memory, const std::string& log_file_name) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream logFile(log_file_name, std::ios::app);
    if (logFile.is_open()) {
        for (const auto& entry : log_memory) {
            logFile << entry << std::endl;
        }
        logFile.close();
    } else {
        std::cerr << "Failed to open log file: " << log_file_name << std::endl;
    }
}

// Очистка старых записей в логе (в памяти)
void cleanOldEntries(std::deque<std::string>& log_memory, int max_age_seconds) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::time_t now = std::time(nullptr);
    while (!log_memory.empty()) {
        std::tm tm = {};
        if (parseTime(log_memory.front().substr(0, 19), tm)) {
            std::time_t entry_time = std::mktime(&tm);
            if (now - entry_time < max_age_seconds) {
                break;
            }
        }
        log_memory.pop_front();
    }
}

// Вычисление среднего значения температуры за последние max_age_seconds
double calculateAverageTemperature(const std::deque<std::string>& log_memory, int max_age_seconds) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::time_t now = std::time(nullptr);
    double sum = 0.0;
    int count = 0;

    for (const auto& entry : log_memory) {
        std::tm tm = {};
        if (parseTime(entry.substr(0, 19), tm)) {
            std::time_t entry_time = std::mktime(&tm);
            if (now - entry_time < max_age_seconds) {
                size_t colon_pos = entry.find_last_of(":");
                if (colon_pos != std::string::npos) {
                    std::string temp_str = entry.substr(colon_pos + 1);
                    temp_str.erase(0, temp_str.find_first_not_of(' '));
                    temp_str.erase(temp_str.find_last_not_of(' ') + 1);

                    try {
                        double temp = std::stod(temp_str);
                        sum += temp;
                        count++;
                    } catch (const std::invalid_argument& e) {
                        std::cerr << "Failed to parse temperature: " << temp_str << " (invalid argument)" << std::endl;
                    } catch (const std::out_of_range& e) {
                        std::cerr << "Failed to parse temperature: " << temp_str << " (out of range)" << std::endl;
                    }
                }
            }
        }
    }

    return (count > 0) ? (sum / count) : 0.0;
}

// Проверка на наличие нулевых байтов в строке
bool containsNullBytes(const std::string& str) {
    return str.find('\x00') != std::string::npos;
}

int main(int argc, char** argv) {
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
            // Валидация данных
            bool is_valid = true;
            for (char ch : mystr) {
                if (!isdigit(ch) && ch != '.' && ch != '-') {
                    is_valid = false;
                    break;
                }
            }
            if (is_valid) {
                std::cout << "Got: " << mystr << std::endl;
                writeToLog(mystr, log_temp_memory); // Запись в память
            }
            cleanOldEntries(log_temp_memory, MAX_TIME_DEFAULT); // Очистка старых записей
        } else {
            std::cout << "Got nothing" << std::endl;
        }

        counter_avg_hour++;
        counter_avg_day++;

        // Каждый час вычисляем среднее значение температуры за последний час
        if (counter_avg_hour >= HOUR) {
            avg_hour_str = to_string(calculateAverageTemperature(log_temp_memory, HOUR));
            writeToLog(avg_hour_str, log_avg_temp_hour_memory); // Запись в память
            counter_avg_hour = 0;
            cleanOldEntries(log_avg_temp_hour_memory, MAX_TIME_HOUR); // Очистка старых записей
        }

        // Каждые 24 часа вычисляем среднее значение температуры за последний день
        if (counter_avg_day >= DAY) {
            avg_day_str = to_string(calculateAverageTemperature(log_temp_memory, DAY));
            writeToLog(avg_day_str, log_avg_temp_day_memory); // Запись в память
            counter_avg_day = 0;
            cleanOldEntries(log_avg_temp_day_memory, MAX_TIME_DAY); // Очистка старых записей
        }

        // Синхронизация логов с диском каждую минуту
        if (counter_avg_hour % 6 == 0) {
            syncLogToDisk(log_temp_memory, "log_temp.log");
            syncLogToDisk(log_avg_temp_hour_memory, "log_avg_temp_hour.log");
            syncLogToDisk(log_avg_temp_day_memory, "log_avg_temp_day.log");
        }
    }

    return 0;
}