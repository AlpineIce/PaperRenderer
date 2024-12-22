#pragma once
#include "../src/PaperRenderer/PaperRenderer.h"

#include <fstream>
#include <functional>
#include <future>
#include <iostream> //for logging callback

std::vector<uint32_t> readFromFile(const std::string& location);