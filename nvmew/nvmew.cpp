// nvmew... a really simple way to talk to OFA NVMe drivers.
// Includes support for the NVME_NO_LOOK_PASS_THROUGH IOCTL. This IOCTL has few sanity checks in terms of what goes to the drive.
//  You can send reads/writes/io queue creation/deletion
//  MIT-License - Charles Machalow
//

#include "stdafx.h"

#include "../source/nvme.h"
#include "../source/nvmeIoctl.h"

#include "Cargparse.h"
#include "Handle.h"
#include "Util.h"

#include <sstream>

#define DBG(thing) if (debug) {fprintf(stderr, "%-35s = 0x%08x (%-8u) \n", #thing, thing, thing);}

#define GET_NVME_PASSTHRU_IOCTL(DW0, DW1, DW10, DW11, DW12, DW13, DW14, DW15, nvm, timeout, dataDirection, dataTransferSize, dataFile, debug, controlCode, passThruBufferSize) __getPassthru(DW0, DW1, DW10, DW11, DW12, DW13, DW14, DW15, nvm, timeout, dataDirection, dataTransferSize, dataFile, debug, controlCode, passThruBufferSize); {
#define FREE_NVME_PASSTHRU_IOCTL(p) free(p);}

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

bool callDeviceIoControl(HANDLE handle, NVME_PASS_THROUGH_IOCTL* passthru, DWORD passThruBufferSize, bool debug, bool silent)
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
		if (!silent) fprintf(stderr, "OS Error: %d\n", GetLastError());
		goto done;
	}

	if (passthru->SrbIoCtrl.ReturnCode != NVME_IOCTL_SUCCESS)
	{
		if (!silent) fprintf(stderr, "SrbIoCtrl.ReturnCode Error: %d\n", passthru->SrbIoCtrl.ReturnCode);
		retVal = false;
		goto done;
	}

	DBG(passthru->CplEntry[0]);
	DBG(passthru->CplEntry[1]);
	DBG(passthru->CplEntry[2]);
	DBG(passthru->CplEntry[3]);

	if (passthru->CplEntry[3] >> 17 != 0)
	{
		if (!silent) fprintf(stderr, "Drive Status Error: 0x%X \n", passthru->CplEntry[3] >> 17);
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
	DWORD timeout, DWORD dataDirection, DWORD dataTransferSize, std::string dataFile, std::string devicePath, bool debug, bool noLook, bool silent)
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

	retVal = callDeviceIoControl(handle.getHandle(), passthru, passThruBufferSize, debug, silent);

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

	DWORD passThruBufferSize = 0;
	NVME_PASS_THROUGH_IOCTL* passthru = GET_NVME_PASSTHRU_IOCTL(0, 0, 0, 0, 0, 0, 0, 0, 0, timeout, 0, 0, "", debug, NVME_RESET_DEVICE, passThruBufferSize);
	BYTE* passThruBuffer = (BYTE*)passthru;

	if (!passthru)
	{
		retVal = false;
		goto done;
	}

	retVal = callDeviceIoControl(handle.getHandle(), passthru, passThruBufferSize, debug, false);

done:
	FREE_NVME_PASSTHRU_IOCTL(passthru);

	DBG(retVal);
	return retVal;
}

bool nvmeControllerRegisters(DWORD timeout, DWORD dataTransferSize, std::string dataFile, std::string devicePath, DWORD dataDirection, bool debug)
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

std::string promptForSelection()
{
	std::vector<std::string> paths;

	for (int i = 0; i < 255; i++)
	{
		std::string testPath = "\\\\.\\SCSI" + std::to_string(i) + ":";
		try
		{
			Handle handle(testPath);
		}
		catch (...)
		{
			continue;
		}

		bool ofa = false;
		bool csm = false;

		csm = nvmePassthru(6, 0, 1, 0, 0, 0, 0, 0, false, 5, 2, 4096, "ic.bin", testPath, false, true, true);
		
		if (!csm)
		{
			ofa = nvmePassthru(6, 0, 1, 0, 0, 0, 0, 0, false, 5, 2, 4096, "ic.bin", testPath, false, false, true);
		}

		if (csm || ofa)
		{
			std::string model = "?";
			std::string fw = "?";
			std::string type = "?";
			PADMIN_IDENTIFY_CONTROLLER pIc = (PADMIN_IDENTIFY_CONTROLLER)READ_FILE("ic.bin", 4096);
			model = std::string((char*)pIc->MN, 40);
			fw = std::string((char*)pIc->FR, 8);
			CLOSE_FILE(pIc);
			remove("ic.bin");
			if (csm)
			{
				type = "CSM NVMe Driver";
			}
			else if (ofa)
			{
				type = "OFA-Compatible Driver";
			}
			fprintf(stderr, "%d) %-30s %-8s %-11s %s\n", (int)paths.size(), model.c_str(), fw.c_str(), testPath.c_str(), type.c_str());
			paths.push_back(testPath);
		}

	}

	int sel;
	std::cerr << "Choose an above index: ";
	std::cin >> sel;

	return paths[sel];
}

int main(int argc, const char** argv)
{
	try
	{
		ArgumentParser parser;
		parser.add_argument(Argument("DW0", "DWord0", "", "DWord 0 of an NVMe Command", "6", false));
		parser.add_argument(Argument("DW1", "DWord1", "", "DWord 1 of an NVMe Command", "0", false));
		parser.add_argument(Argument("DW10", "DWord10", "", "DWord 10 of an NVMe Command", "1", false));
		parser.add_argument(Argument("DW11", "DWord11", "", "DWord 11 of an NVMe Command", "0", false));
		parser.add_argument(Argument("DW12", "DWord12", "", "DWord 12 of an NVMe Command", "0", false));
		parser.add_argument(Argument("DW13", "DWord13", "", "DWord 13 of an NVMe Command", "0", false));
		parser.add_argument(Argument("DW14", "DWord14", "", "DWord 14 of an NVMe Command", "0", false));
		parser.add_argument(Argument("DW15", "DWord15", "", "DWord 15 of an NVMe Command", "0", false));
		parser.add_argument(Argument("nvm", "NVM", "store_true", "If given, send command as NVM instead of Admin", "", false));
		parser.add_argument(Argument("debug", "debug", "store_true", "If given, Send debug info to stderr", "", false));
		parser.add_argument(Argument("noLook", "noLook", "store_true", "If given, Use NO_LOOK IOCTL", "", false));
		parser.add_argument(Argument("timeout", "timeout", "", "Timeout in seconds", "60", false));
		parser.add_argument(Argument("dataDirection", "dataDirection", "", "0:non data, 1:write, 2:read", "2", false));
		parser.add_argument(Argument("dataTransferSize", "dataTransferSize", "", "In bytes", "8192", false));
		parser.add_argument(Argument("dataFile", "dataFile", "", "Location of binary file", "", false));
		parser.add_argument(Argument("devicePath", "devicePath", "", "Path to device", "", false));

		// Actions
		parser.add_argument(Argument("passthru", "passthru", "store_true", "If given, Do an NVMe passthru command", "false", false));
		parser.add_argument(Argument("controllerRegisters", "controllerRegs", "store_true", "If given, target the controller registers", "false", false));
		parser.add_argument(Argument("reset", "controllerReset", "store_true", "If given, do an NVMe Controller Reset", "false", false));

		parser.parse_args(argv, argc);

		bool passthru = parser.getBooleanValue("passthru");
		bool controllerRegisters = parser.getBooleanValue("controllerRegisters");
		bool reset = parser.getBooleanValue("reset");

		// Make sure only one action was given
		if (!(passthru ^ controllerRegisters ^ reset))
		{
			throw std::runtime_error("Give one of the following: passthru, controllerRegisters, reset");
		}

		bool success = false;

		std::string devicePath = parser.getStringValue("devicePath");
		if (devicePath.size() == 0)
		{
			devicePath = promptForSelection();
		}

		if (passthru)
		{
			success = nvmePassthru(
				parser.getNumericValue("DW0"),
				parser.getNumericValue("DW1"),
				parser.getNumericValue("DW10"),
				parser.getNumericValue("DW11"),
				parser.getNumericValue("DW12"),
				parser.getNumericValue("DW13"),
				parser.getNumericValue("DW14"),
				parser.getNumericValue("DW15"),
				parser.getBooleanValue("NVM"),
				parser.getNumericValue("timeout"),
				parser.getNumericValue("dataDirection"),
				parser.getNumericValue("dataTransferSize"),
				parser.getStringValue("dataFile"),
				devicePath,
				parser.getBooleanValue("debug"),
				parser.getBooleanValue("noLook"),
				false
			);
		}
		else if (reset)
		{
			success = nvmeReset(
				devicePath,
				parser.getBooleanValue("debug"),
				parser.getNumericValue("timeout")
			);
		}
		else if (controllerRegisters)
		{
			success = nvmeControllerRegisters(
				parser.getNumericValue("timeout"),
				parser.getNumericValue("dataTransferSize"),
				parser.getStringValue("dataFile"),
				devicePath,
				parser.getNumericValue("dataDirection"),
				parser.getBooleanValue("debug")
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
	catch (std::exception ex)
	{
		fprintf(stderr, "Exception Caught! : %s\n", ex.what());
		return EXIT_FAILURE;
	}
}

