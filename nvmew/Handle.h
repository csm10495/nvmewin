#pragma once

#include <Windows.h>
#include <string>

class Handle
{
public:
	Handle();
	Handle(std::string path);
	Handle(const Handle &other);
	Handle &operator=(const Handle &other);
	~Handle();

	std::string &getPath();
	HANDLE &getHandle();
private:
	void setup();

	HANDLE handle;
	std::string path;
};