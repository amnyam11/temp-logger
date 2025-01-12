#include "my_serial.hpp"
#include <sstream>              // std::stringstream
#include <iostream>             // std::cout
#include <random>
#include <ctime>

template<class T> std::string to_string(const T& v)
{
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

void csleep(double timeout) {
#if defined (WIN32)
    if (timeout <= 0.0)
        ::Sleep(INFINITE);
    else
        ::Sleep((DWORD)(timeout * 1e3));
#else
    if (timeout <= 0.0)
        pause();
    else {
        struct timespec t;
        t.tv_sec = (int)timeout;
        t.tv_nsec = (int)((timeout - t.tv_sec)*1e9);
        nanosleep(&t, NULL);
    }
#endif
}

double random_number() {
    static std::random_device rd;  // Источник случайных чисел (seed)
    static std::mt19937 gen(rd()); // Генератор Mersenne Twister

    // Определение диапазона [a, b]
    std::uniform_real_distribution<> distrib(20.0, 30.0);

    // Генерация случайного double в диапазоне [a, b]
    double random_value = distrib(gen);

    // Округление до десятых
    random_value = std::round(random_value * 10) / 10;

    return random_value;
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
    for (;;) {
        mystr = std::string("Iteration ") + to_string(random_number());
        smport << mystr;
        csleep(1.0);
    }

    return 0;
}