// Anyxbox first-run report: what a PE needs and what is missing. GPL-2.0.
#pragma once
#include <string>
#include "loader/pe_image.h"
namespace anyxbox { std::string pe_report(const LoadedImage& img); }
