#include "stdafx.h"
#include "Util.h"

BYTE* __readFile(std::string filePath, size_t numBytes)
{
	FILE* file = fopen(filePath.c_str(), "rb");
	if (!file)
	{
		throw std::runtime_error("Could not fopen() file: " + filePath);
	}

	BYTE* retBuffer = (BYTE*)calloc(numBytes, 1);
	if (fread(retBuffer, 1, numBytes, file) != numBytes)
	{
		free(retBuffer);
		fclose(file);
		throw std::runtime_error("Unable to read the expected amount of data");
	}
	return retBuffer;
	
}
