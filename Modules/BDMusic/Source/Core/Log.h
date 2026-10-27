#pragma once
#include "Base.h"

#include <format>
#include <iostream>

//Simple logging
#define LOG_INFO(...)  (std::cout << std::format(__VA_ARGS__) << std::endl)
#define LOG_WARN(...)  (std::cout << "Warning: " << std::format(__VA_ARGS__) << std::endl)
#define LOG_ERROR(...) (std::cout << "Error: " << std::format(__VA_ARGS__) << std::endl)
