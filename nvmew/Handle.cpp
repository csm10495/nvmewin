#include "stdafx.h"
#include "Handle.h"

#include <stdexcept>

Handle::Handle()
{
	path = "";
	handle = nullptr;
}

Handle::Handle(std::string path) : Handle::Handle()
{
	this->path = path;
	setup();
}

Handle::Handle(const Handle & other)
{
	path = other.path;
	setup();
}

Handle & Handle::operator=(const Handle & other)
{
	if (this != &other)
	{
		new(this) Handle(other);
	}
	return *this;
}

Handle::~Handle()
{
	if (handle)
	{
		CloseHandle(handle);
		handle = nullptr;
	}
	path = "";
}

std::string & Handle::getPath()
{
	return path;
}

HANDLE & Handle::getHandle()
{
	return handle;
}

void Handle::setup()
{
	if (!handle)
	{
		handle = CreateFile(this->path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		if (handle == INVALID_HANDLE_VALUE)
		{
			fprintf(stderr, "Could not create handle to: %s\n", path.c_str());
			throw std::runtime_error("");
		}
	}
}
