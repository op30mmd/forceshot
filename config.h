// config.h
// This file contains preprocessor directives to ensure the correct Windows API version is targeted.
// It must be included before any other headers, especially windows.h.

#pragma once

// Target Windows 10 and later.
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
