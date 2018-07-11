#pragma once

#include "stdafx.h"

BYTE* __readFile(std::string filePath, size_t numBytes);

#define READ_FILE(filePath, numBytes) __readFile(filePath, numBytes); {
#define CLOSE_FILE(b) free(b); }