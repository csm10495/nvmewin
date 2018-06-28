// nvmew... a really simple way to talk to OFA NVMe drivers.
// Includes support for the NVME_NO_LOOK_PASS_THROUGH IOCTL. This IOCTL has few sanity checks in terms of what goes to the drive.
//  You can send reads/writes/io queue creation/deletion
//  MIT-License - Charles Machalow
//

#include "stdafx.h"

#include "argparse/argparse.hpp"
#include "../source/nvmeIoctl.h"

#include "Handle.h"

#include <sstream>

#define DBG(thing) if (debug) {fprintf(stderr, "%-35s = 0x%08x (%-8u) \n", #thing, thing, thing);}

std::string getControlCodeString(DWORD controlCode)
{
	switch (controlCode)
	{
	case NVME_PASS_THROUGH_SRB_IO_CODE:
		return "NVME_PASS_THROUGH_SRB_IO_CODE";
	case NVME_RESET_DEVICE:
		return "NVME_RESET_DEVICE";
	case NVME_NO_LOOK_PASS_THROUGH:
		return "NVME_NO_LOOK_PASS_THROUGH";
	case NVME_CONTROLLER_REGISTERS:
		return "NVME_CONTROLLER_REGISTERS";
	}
	return "Unknown";
}

NVME_PASS_THROUGH_IOCTL* __getPassthru(DWORD DW0, DWORD DW1, DWORD DW10, DWORD DW11, DWORD DW12, DWORD DW13, DWORD DW14, DWORD DW15, bool nvm,
	DWORD timeout, DWORD dataDirection, DWORD dataTransferSize, std::string dataFile, bool debug, DWORD controlCode, DWORD& passThruBufferSize)
{
	bool shouldFree = false; // free on error, return NULL

	passThruBufferSize = sizeof(NVME_PASS_THROUGH_IOCTL) + dataTransferSize;
	BYTE* passThruBuffer = (BYTE*)calloc(passThruBufferSize, 1);
	NVME_PASS_THROUGH_IOCTL* passthru = (NVME_PASS_THROUGH_IOCTL*)passThruBuffer;
	DBG(passThruBufferSize);

	// Setup SRB
	passthru->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
	memcpy(passthru->SrbIoCtrl.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
	passthru->SrbIoCtrl.Timeout = timeout;
	passthru->SrbIoCtrl.ControlCode = controlCode;
	passthru->SrbIoCtrl.Length = passThruBufferSize - sizeof(SRB_IO_CONTROL);
	DBG(passthru->SrbIoCtrl.Timeout);
	DBG(passthru->SrbIoCtrl.ControlCode);
	if (debug)
	{
		fprintf(stderr, "%-35s = %s\n", "ControlCode Parsing", getControlCodeString(passthru->SrbIoCtrl.ControlCode).c_str());
	}
	DBG(passthru->SrbIoCtrl.Length);

	// Setup NVMe Command
	passthru->NVMeCmd[0] = DW0;
	passthru->NVMeCmd[1] = DW1;
	passthru->NVMeCmd[10] = DW10;
	passthru->NVMeCmd[11] = DW11;
	passthru->NVMeCmd[12] = DW12;
	passthru->NVMeCmd[13] = DW13;
	passthru->NVMeCmd[14] = DW14;
	passthru->NVMeCmd[15] = DW15;
	DBG(passthru->NVMeCmd[0]);
	DBG(passthru->NVMeCmd[1]);
	DBG(passthru->NVMeCmd[10]);
	DBG(passthru->NVMeCmd[11]);
	DBG(passthru->NVMeCmd[12]);
	DBG(passthru->NVMeCmd[13]);
	DBG(passthru->NVMeCmd[14]);
	DBG(passthru->NVMeCmd[15]);

	// Setup rest of passthru structure
	passthru->Direction = dataDirection;
	passthru->QueueId = nvm;
	passthru->ReturnBufferLen = passThruBufferSize;
	passthru->DataBufferLen = dataTransferSize;
	DBG(passthru->Direction);
	DBG(passthru->QueueId);
	DBG(passthru->ReturnBufferLen);
	DBG(passthru->DataBufferLen);

	if (passthru->Direction == NVME_BI_DIRECTION)
	{
		fprintf(stderr, "Bi directional is not supported.\n");
		shouldFree = true;
		goto done;
	}

	if (passthru->Direction == NVME_FROM_HOST_TO_DEV)
	{
		passthru->DataBufferLen = dataTransferSize;
		if (dataFile.size())
		{
			FILE* file = fopen(dataFile.c_str(), "rb");
			if (!file)
			{
				fprintf(stderr, "Unable to open file: %s\n", dataFile.c_str());
				shouldFree = true;
				goto done;
			}

			BYTE* whereDataGoes = passThruBuffer + sizeof(NVME_PASS_THROUGH_IOCTL) - 1;
			if (fread(whereDataGoes, 1, passthru->DataBufferLen, file) != passthru->DataBufferLen)
			{
				fprintf(stderr, "Unable to read the expected amount of data (%d bytes) from file: %s\n", passthru->DataBufferLen, dataFile.c_str());
				shouldFree = true;
				goto done;
			}
			fclose(file);
		}
	}

done:
	if (shouldFree)
	{
		free(passthru);
		passthru = NULL;
	}

	return passthru;
}

#define GET_NVME_PASSTHRU_IOCTL(DW0, DW1, DW10, DW11, DW12, DW13, DW14, DW15, nvm, timeout, dataDirection, dataTransferSize, dataFile, debug, controlCode, passThruBufferSize) __getPassthru(DW0, DW1, DW10, DW11, DW12, DW13, DW14, DW15, nvm, timeout, dataDirection, dataTransferSize, dataFile, debug, controlCode, passThruBufferSize); {
#define FREE_NVME_PASSTHRU_IOCTL(p) free(p);}

bool callDeviceIoControl(HANDLE handle, NVME_PASS_THROUGH_IOCTL* passthru, DWORD passThruBufferSize, bool debug)
{
	DWORD bytesReturned;
	bool retVal = DeviceIoControl(
		handle,
		IOCTL_SCSI_MINIPORT,
		passthru,
		passThruBufferSize,
		passthru,
		passThruBufferSize,
		&bytesReturned,
		NULL
	) != 0;
	DBG(bytesReturned);
	DBG(passthru->SrbIoCtrl.ReturnCode);
	DBG(GetLastError());

	if (!retVal)
	{
		fprintf(stderr, "OS Error: %d\n", GetLastError());
		goto done;
	}

	if (passthru->SrbIoCtrl.ReturnCode != NVME_IOCTL_SUCCESS)
	{
		fprintf(stderr, "SrbIoCtrl.ReturnCode Error: %d\n", passthru->SrbIoCtrl.ReturnCode);
		retVal = false;
		goto done;
	}

	DBG(passthru->CplEntry[0]);
	DBG(passthru->CplEntry[1]);
	DBG(passthru->CplEntry[2]);
	DBG(passthru->CplEntry[3]);

	if (passthru->CplEntry[3] >> 17 != 0)
	{
		fprintf(stderr, "Drive Status Error: 0x%X \n", passthru->CplEntry[3] >> 17);
		retVal = false;
		goto done;
	}

done:
	return retVal;
}

bool processOutputData(NVME_PASS_THROUGH_IOCTL* passthru, std::string dataFile)
{
	BYTE* passThruBuffer = (BYTE*)passthru;
	bool retVal = true;
	// Command succeeded!
	if (passthru->Direction == NVME_FROM_DEV_TO_HOST)
	{
		BYTE* whereDataGoes = passThruBuffer + sizeof(NVME_PASS_THROUGH_IOCTL) - 1;

		if (dataFile.size())
		{
			FILE* file = fopen(dataFile.c_str(), "wb");
			if (!file)
			{
				fprintf(stderr, "Unable to open file: %s\n", dataFile.c_str());
				retVal = false;
				goto done;
			}

			if (fwrite(whereDataGoes, 1, passthru->ReturnBufferLen, file) != passthru->ReturnBufferLen)
			{
				fprintf(stderr, "Unable to write the expected amount of data (%d bytes) to file: %s\n", passthru->ReturnBufferLen, dataFile.c_str());
				fclose(file);
				retVal = false;
				goto done;
			}
			fclose(file);
		}
		else
		{
			// Send to stdout
			if (fwrite(whereDataGoes, 1, passthru->DataBufferLen, stdout) != passthru->DataBufferLen)
			{
				fprintf(stderr, "Unable to write all data to (%d bytes) stdout\n", passthru->DataBufferLen);
				retVal = false;
				goto done;
			}
		}
	}

done:
	return retVal;
}

bool nvmePassthru(DWORD DW0, DWORD DW1, DWORD DW10, DWORD DW11, DWORD DW12, DWORD DW13, DWORD DW14, DWORD DW15, bool nvm,
	DWORD timeout, DWORD dataDirection, DWORD dataTransferSize, std::string dataFile, std::string devicePath, bool debug, bool noLook)
{
	bool retVal = true;
	Handle handle(devicePath);

	DWORD controlCode = NVME_PASS_THROUGH_SRB_IO_CODE;
	if (noLook)
		controlCode = NVME_NO_LOOK_PASS_THROUGH;
	
	DWORD passThruBufferSize = 0;
	NVME_PASS_THROUGH_IOCTL* passthru = GET_NVME_PASSTHRU_IOCTL(DW0, DW1, DW10, DW11, DW12, DW13, DW14, DW15, nvm, timeout, dataDirection, dataTransferSize, dataFile, debug, controlCode, passThruBufferSize);
	BYTE* passThruBuffer = (BYTE*)passthru;

	if (!passthru)
	{
		retVal = false;
		goto done;
	}
	
	retVal = callDeviceIoControl(handle.getHandle(), passthru, passThruBufferSize, debug);

	if (retVal)
	{
		processOutputData(passthru, dataFile);
	}

done:
	FREE_NVME_PASSTHRU_IOCTL(passthru);

	DBG(retVal);
	return retVal;
}

bool nvmeReset(std::string devicePath, bool debug, DWORD timeout)
{
	bool retVal = true;
	Handle handle(devicePath);

	DWORD passThruBufferSize = sizeof(NVME_PASS_THROUGH_IOCTL);
	BYTE* passThruBuffer = (BYTE*)calloc(passThruBufferSize, 1);
	NVME_PASS_THROUGH_IOCTL* passthru = (NVME_PASS_THROUGH_IOCTL*)passThruBuffer;
	DBG(passThruBufferSize);

	// Setup SRB
	passthru->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
	memcpy(passthru->SrbIoCtrl.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
	passthru->SrbIoCtrl.Timeout = timeout;
	passthru->SrbIoCtrl.ControlCode = NVME_RESET_DEVICE;
	passthru->SrbIoCtrl.Length = passThruBufferSize - sizeof(SRB_IO_CONTROL);
	DBG(NVME_RESET_DEVICE);
	DBG(passthru->SrbIoCtrl.Timeout);
	DBG(passthru->SrbIoCtrl.ControlCode);
	DBG(passthru->SrbIoCtrl.Length);

	DWORD bytesReturned;
	retVal = DeviceIoControl(
		handle.getHandle(),
		IOCTL_SCSI_MINIPORT,
		passThruBuffer,
		passThruBufferSize,
		passThruBuffer,
		passThruBufferSize,
		&bytesReturned,
		NULL
	) != 0;
	DBG(bytesReturned);
	DBG(passthru->SrbIoCtrl.ReturnCode);
	DBG(GetLastError());

	if (!retVal)
	{
		fprintf(stderr, "OS Error: %d\n", GetLastError());
		goto done;
	}

	if (passthru->SrbIoCtrl.ReturnCode != NVME_IOCTL_SUCCESS)
	{
		fprintf(stderr, "SrbIoCtrl.ReturnCode Error: %d\n", passthru->SrbIoCtrl.ReturnCode);
		retVal = false;
		goto done;
	}

done:
	free(passThruBuffer);

	DBG(retVal);
	return retVal;
}

bool nvmeControllerRegister(DWORD timeout, DWORD dataTransferSize, std::string dataFile, std::string devicePath, DWORD dataDirection, bool debug)
{
	bool retVal = true;
	Handle handle(devicePath);

	DWORD passThruBufferSize = sizeof(NVME_PASS_THROUGH_IOCTL) + dataTransferSize;
	BYTE* passThruBuffer = (BYTE*)calloc(passThruBufferSize, 1);
	NVME_PASS_THROUGH_IOCTL* passthru = (NVME_PASS_THROUGH_IOCTL*)passThruBuffer;
	DBG(passThruBufferSize);

	// Setup SRB
	passthru->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
	memcpy(passthru->SrbIoCtrl.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
	passthru->SrbIoCtrl.Timeout = timeout;
	passthru->SrbIoCtrl.ControlCode = NVME_CONTROLLER_REGISTERS;
	DBG(NVME_CONTROLLER_REGISTERS);
	passthru->SrbIoCtrl.Length = passThruBufferSize - sizeof(SRB_IO_CONTROL);
	DBG(passthru->SrbIoCtrl.Timeout);
	DBG(passthru->SrbIoCtrl.ControlCode);
	DBG(passthru->SrbIoCtrl.Length);

	// Setup rest of passthru structure
	passthru->Direction = dataDirection;
	passthru->ReturnBufferLen = passThruBufferSize;
	passthru->DataBufferLen = dataTransferSize;
	DBG(passthru->Direction);
	DBG(passthru->ReturnBufferLen);
	DBG(passthru->DataBufferLen);

	if (passthru->Direction == NVME_FROM_HOST_TO_DEV)
	{
		passthru->DataBufferLen = dataTransferSize;
		if (dataFile.size())
		{
			FILE* file = fopen(dataFile.c_str(), "rb");
			if (!file)
			{
				fprintf(stderr, "Unable to open file: %s\n", dataFile.c_str());
				retVal = false;
				goto done;
			}

			BYTE* whereDataGoes = passThruBuffer + sizeof(NVME_PASS_THROUGH_IOCTL) - 1;
			if (fread(whereDataGoes, 1, passthru->DataBufferLen, file) != passthru->DataBufferLen)
			{
				fprintf(stderr, "Unable to read the expected amount of data (%d bytes) from file: %s\n", passthru->DataBufferLen, dataFile.c_str());
				retVal = false;
				goto done;
			}
			fclose(file);
		}

	}
	else if (passthru->Direction == NVME_BI_DIRECTION)
	{
		fprintf(stderr, "Bi directional is not supported.\n");
		retVal = false;
		goto done;
	}

	DWORD bytesReturned;
	retVal = DeviceIoControl(
		handle.getHandle(),
		IOCTL_SCSI_MINIPORT,
		passThruBuffer,
		passThruBufferSize,
		passThruBuffer,
		passThruBufferSize,
		&bytesReturned,
		NULL
	) != 0;
	DBG(bytesReturned);
	DBG(passthru->SrbIoCtrl.ReturnCode);
	DBG(GetLastError());

	if (!retVal)
	{
		fprintf(stderr, "OS Error: %d\n", GetLastError());
		goto done;
	}

	if (passthru->SrbIoCtrl.ReturnCode != NVME_IOCTL_SUCCESS)
	{
		fprintf(stderr, "SrbIoCtrl.ReturnCode Error: %d\n", passthru->SrbIoCtrl.ReturnCode);
		retVal = false;
		goto done;
	}

	// Command succeeded!
	if (dataFile.size())
	{
		FILE* file = fopen(dataFile.c_str(), "wb");
		if (!file)
		{
			fprintf(stderr, "Unable to open file: %s\n", dataFile.c_str());
			retVal = false;
			goto done;
		}
		BYTE* whereDataGoes = passThruBuffer + sizeof(NVME_PASS_THROUGH_IOCTL) - 1;

		if (fwrite(whereDataGoes, 1, passthru->ReturnBufferLen, file) != passthru->ReturnBufferLen)
		{
			fprintf(stderr, "Unable to write the expected amount of data (%d bytes) to file: %s\n", passthru->ReturnBufferLen, dataFile.c_str());
			retVal = false;
			goto done;
		}
		fclose(file);
	}
	else
	{
		BYTE* whereDataGoes = passThruBuffer + sizeof(NVME_PASS_THROUGH_IOCTL) - 1;

		// Send to stdout
		if (fwrite(whereDataGoes, 1, passthru->DataBufferLen, stdout) != passthru->DataBufferLen)
		{
			fprintf(stderr, "Unable to write all data to (%d bytes) stdout\n", passthru->DataBufferLen);
			retVal = false;
			goto done;
		}
	}


done:
	free(passThruBuffer);

	DBG(retVal);
	return retVal;
}


DWORD retrieveDWORDWithDefault(ArgumentParser &parser, std::string key, DWORD _default)
{
	try
	{
		// for some reason we always need to request a string and manually convert it to long
		std::string v = parser.retrieve<std::string>(key);
		if (v.size() == 0)
		{
			throw std::bad_cast();
		}

		if (v[1] == 'x')
		{
			// hex?
			std::stringstream s;
			DWORD d;
			s << std::hex << v.substr(2);
			s >> d;
			v = std::to_string(d);
		}

		return (DWORD)atol(v.c_str());
	}
	catch (std::exception ex)
	{
		return _default;
	}
}

std::string retrieveStringWithDefault(ArgumentParser &parser, std::string key, std::string _default)
{
	try
	{
		return parser.retrieve<std::string>(key);
	}
	catch (std::exception ex)
	{
		return _default;
	}
}

bool retrieveBool(int argc, const char** argv, std::string key)
{
	// can't get the parser library to work... so do it manually
	for (int i = 0; i < argc; i++)
	{
		if (std::string(argv[i]) == "--" + key)
		{
			return true;
		}
	}
	return false;
}

int main(int argc, const char** argv)
{
	ArgumentParser parser;
	parser.addArgument("--DW0", 1);
	parser.addArgument("--DW1", 1);
	parser.addArgument("--DW10", 1);
	parser.addArgument("--DW11", 1);
	parser.addArgument("--DW12", 1);
	parser.addArgument("--DW13", 1);
	parser.addArgument("--DW14", 1);
	parser.addArgument("--DW15", 1);
	parser.addArgument("--NVM");
	parser.addArgument("--debug");
	parser.addArgument("--timeout", 1);
	parser.addArgument("--dataDirection", 1);
	parser.addArgument("--dataTransferSize", 1);
	parser.addArgument("--dataFile", 1);
	parser.addArgument("--devicePath", 1, false);
	parser.addArgument("--noLook");
	parser.addArgument("--reset");
	parser.addArgument("--controllerRegisters");

	parser.parse(argc, argv);

	bool doReset = retrieveBool(argc, argv, "reset");
	bool doControllerRegisters = retrieveBool(argc, argv, "controllerRegisters");

	if (doReset && doControllerRegisters)
	{
		fprintf(stderr, "Can't use --reset and --controllerRegisters together!\n");
		return EXIT_FAILURE;
	}

	bool success = false;

	if (doReset)
	{
		success = nvmeReset(
			parser.retrieve<std::string>("devicePath"),
			retrieveBool(argc, argv, "debug"),
			retrieveDWORDWithDefault(parser, "timeout", 60)
		);
	}
	else if (doControllerRegisters)
	{
		success = nvmeControllerRegister(
			retrieveDWORDWithDefault(parser, "timeout", 60),
			retrieveDWORDWithDefault(parser, "dataTransferSize", 5002),
			retrieveStringWithDefault(parser, "dataFile", ""),
			parser.retrieve<std::string>("devicePath"),
			retrieveDWORDWithDefault(parser, "dataDirection", NVME_FROM_DEV_TO_HOST),
			retrieveBool(argc, argv, "debug")
		);
	}
	else
	{
		success = nvmePassthru(
			retrieveDWORDWithDefault(parser, "DW0", 0),
			retrieveDWORDWithDefault(parser, "DW1", 0),
			retrieveDWORDWithDefault(parser, "DW10", 0),
			retrieveDWORDWithDefault(parser, "DW11", 0),
			retrieveDWORDWithDefault(parser, "DW12", 0),
			retrieveDWORDWithDefault(parser, "DW13", 0),
			retrieveDWORDWithDefault(parser, "DW14", 0),
			retrieveDWORDWithDefault(parser, "DW15", 0),
			retrieveBool(argc, argv, "NVM"),
			retrieveDWORDWithDefault(parser, "timeout", 60),
			retrieveDWORDWithDefault(parser, "dataDirection", 2),
			retrieveDWORDWithDefault(parser, "dataTransferSize", 4096),
			retrieveStringWithDefault(parser, "dataFile", ""),
			parser.retrieve<std::string>("devicePath"),
			retrieveBool(argc, argv, "debug"),
			retrieveBool(argc, argv, "noLook")
		);
	}

	if (success)
	{
		fprintf(stderr, "Success\n");
		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Failure\n");
		return EXIT_FAILURE;
	}
}

