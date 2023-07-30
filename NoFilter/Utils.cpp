#include "Utils.h"

wstring GetPathFromHandle(const HANDLE ObjectHandle)
{
	// Retrieve the name of the object from the handle
	DWORD objectNameReturnedSize = 0;
	char objectName[MAX_PATH + sizeof(OBJECT_NAME_INFORMATION)];
	const NTSTATUS status = NtQueryObject(ObjectHandle, ObjectNameInformation, objectName, sizeof(objectName), &objectNameReturnedSize);
	if ((!NT_SUCCESS(status)) || 0 == objectNameReturnedSize)
	{
		CloseHandle(ObjectHandle);
		ThrowException("[GetPathFromHandle] NtQueryObject failed", status);
	}

	// Build a wstring from the returned name
	const auto [ObjectNameLength, ObjectNameMaximumLength, ObjectNameBuffer] = reinterpret_cast<POBJECT_NAME_INFORMATION>(objectName)->Name;
	wstring ObjectPathWstring(ObjectNameBuffer, ObjectNameLength / sizeof(WCHAR));
	return ObjectPathWstring;
}

bool LaunchProcess(const HANDLE TokenHandle, const wstring& ProcessPath)
{
	STARTUPINFOW startupInfo = {};
	PROCESS_INFORMATION processInfo = {};
	startupInfo.cb = sizeof(STARTUPINFOW);

	// Copy process name to RW buffer
	const DWORD bufferSize = (ProcessPath.size() + 1) * sizeof(wchar_t);
	const auto processPathBuffer = std::unique_ptr<BYTE>(new BYTE[bufferSize]);
	ZeroMemory(processPathBuffer.get(), bufferSize);
	memcpy_s(processPathBuffer.get(), bufferSize, ProcessPath.data(), bufferSize);
	if (!::CreateProcessWithTokenW(TokenHandle, LOGON_NETCREDENTIALS_ONLY, nullptr, reinterpret_cast<LPWSTR>(processPathBuffer.get()), 0, nullptr, nullptr, &startupInfo, &processInfo))
		ThrowException("[LaunchElevatedProcess] CreateProcessWithTokenW failed", GetLastError());

	wcout << L"Process started. PID: " << processInfo.dwProcessId << endl;
	return true;
}

bool LaunchCMD(const HANDLE TokenHandle)
{
	return LaunchProcess(TokenHandle, L"cmd.exe");
}

void EnablePrivilege(const HANDLE TokenHandle, const LPCWSTR Privilege)
{
	TOKEN_PRIVILEGES tokenPrivileges = {};

	if (!::LookupPrivilegeValueW(nullptr, Privilege, &tokenPrivileges.Privileges[0].Luid))
	{
		CloseHandle(TokenHandle);
		ThrowException("[EnablePrivilege] LookupPrivilegeValueW failed", GetLastError());
	}

	tokenPrivileges.PrivilegeCount = 1;
	tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!::AdjustTokenPrivileges(TokenHandle, 0, &tokenPrivileges, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
	{
		CloseHandle(TokenHandle);
		ThrowException("[EnablePrivilege] AdjustTokenPrivileges failed", GetLastError());
	}
}

void EnablePrivilegeCurrentProcess(const LPCWSTR Privilege)
{
	HANDLE tokenHandle;
	if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenHandle))
	{
		EnablePrivilege(tokenHandle, Privilege);
		CloseHandle(tokenHandle);
	}
	else
		ThrowException("[EnablePrivilegeCurrentProcess] OpenProcessToken failed", GetLastError());
}

void EnableDebugPrivilegeCurrentProcess()
{
	EnablePrivilegeCurrentProcess(SE_DEBUG_NAME);
}

DWORD GetServicePid(const wstring ServiceName)
{
	const SC_HANDLE controlManagerHandle = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (nullptr == controlManagerHandle)
		ThrowException("[GetServicePid] Connecting to Service Control Manager failed", GetLastError());

	const SC_HANDLE serviceHandle = OpenServiceW(controlManagerHandle, ServiceName.c_str(), SERVICE_QUERY_STATUS);
	CloseServiceHandle(controlManagerHandle);
	if (nullptr == serviceHandle)
		ThrowException("[GetServicePid] Opening service handle failed", GetLastError());

	SERVICE_STATUS_PROCESS procInfo;
	DWORD bytesNeeded;
	if (!QueryServiceStatusEx(serviceHandle, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&procInfo), sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded))
	{
		CloseServiceHandle(serviceHandle);
		ThrowException("[GetServicePid] Querying service status failed", GetLastError());
	}

	CloseServiceHandle(serviceHandle);
	return procInfo.dwProcessId;
}

bool GetProcessHandleTable(const HANDLE ProcessHandle, unique_ptr<BYTE>& HandleSnapshotInfoBuffer)
{
	ULONG handleSnapshotInfoLength = sizeof(PROCESS_HANDLE_SNAPSHOT_INFORMATION);
	NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;
	do {
		// Allocate more memory until NtQueryInformationProcess succeeds
		handleSnapshotInfoLength += (sizeof(PROCESS_HANDLE_TABLE_ENTRY_INFO) * 16);
		HandleSnapshotInfoBuffer = unique_ptr<BYTE>(new BYTE[handleSnapshotInfoLength]);
		if (!HandleSnapshotInfoBuffer)
			ThrowException("[GetProcessHandleTable] Failed to allocate buffer for handle table", STATUS_NO_MEMORY);

		status = NtQueryInformationProcess(ProcessHandle, static_cast<PROCESSINFOCLASS>(ProcessHandleInformation), HandleSnapshotInfoBuffer.get(), handleSnapshotInfoLength, &handleSnapshotInfoLength);
		if (NT_SUCCESS(status))
			break;
		
		if (status == STATUS_INFO_LENGTH_MISMATCH)
			continue;

		// Throw exception if the status is not STATUS_SUCCESS or STATUS_INFO_LENGTH_MISMATCH
		ThrowException("[GetProcessHandleTable] NtQueryInformationProcess failed", status);
	} while (status == STATUS_INFO_LENGTH_MISMATCH);
	return true;
}

bool GetSystemHandleTable(unique_ptr<BYTE>& HandleSnapshotInfoBuffer)
{
	ULONG handleSnapshotInfoLength = sizeof(SYSTEM_HANDLE_INFORMATION_EX);
	NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;
	do {
		// Allocate more memory until NtQuerySystemInformation succeeds
		handleSnapshotInfoLength += (sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX) * 16);
		HandleSnapshotInfoBuffer = unique_ptr<BYTE>(new BYTE[handleSnapshotInfoLength]);
		if (!HandleSnapshotInfoBuffer)
			ThrowException("[GetSystemHandleTable] Failed to allocate buffer for handle table", STATUS_NO_MEMORY);

		status = NtQuerySystemInformation(SystemExtendedHandleInformation, HandleSnapshotInfoBuffer.get(), handleSnapshotInfoLength, &handleSnapshotInfoLength);
		if (NT_SUCCESS(status))
			break;

		if (status == STATUS_INFO_LENGTH_MISMATCH)
			continue;

		// Throw exception if the status is not STATUS_SUCCESS or STATUS_INFO_LENGTH_MISMATCH
		ThrowException("[GetSystemHandleTable] NtQuerySystemInformation failed", status);
	} while (status == STATUS_INFO_LENGTH_MISMATCH);
	return true;
}

vector<HANDLE> GetHandlesFromProcess(DWORD ProcessId, ULONG TypeIndex, PACCESS_MASK GrantedAccess)
{
	vector<HANDLE> handlesVector;

	if (IsWindows8OrGreater())
	{
		const HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, false, ProcessId);
		if (nullptr == processHandle)
			ThrowException("[FindObjectHandle] OpenProcess failed", GetLastError());

		// Retrieve the handle table of the process
		unique_ptr<BYTE> handleSnapshotInfoBuffer;
		GetProcessHandleTable(processHandle, handleSnapshotInfoBuffer);
		CloseHandle(processHandle);
		const auto handleSnapshotInfo = reinterpret_cast<PPROCESS_HANDLE_SNAPSHOT_INFORMATION>(handleSnapshotInfoBuffer.get());

		// Iterate the table and find handles with the correct TypeIndex and GrantedAccess
		for (int i = 0; i < handleSnapshotInfo->NumberOfHandles; i++)
		{
			const PROCESS_HANDLE_TABLE_ENTRY_INFO* currentHandleInfo = &handleSnapshotInfo->Handles[i];
			if (currentHandleInfo->ObjectTypeIndex != TypeIndex)
				continue;
			if (GrantedAccess != nullptr && *GrantedAccess != currentHandleInfo->GrantedAccess)
				continue;
			handlesVector.push_back(currentHandleInfo->HandleValue);
		}
	}
	else
	{
		// Retrieve the handle table of the entire system
		unique_ptr<BYTE> handleSnapshotInfoBuffer;
		GetSystemHandleTable(handleSnapshotInfoBuffer);
		const auto handleSnapshotInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(handleSnapshotInfoBuffer.get());

		// Iterate the table and find handles with the correct Pid, TypeIndex and GrantedAccess
		for (int i = 0; i < handleSnapshotInfo->NumberOfHandles; i++)
		{
			const SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX* currentHandleInfo = &handleSnapshotInfo->Handles[i];
			if (currentHandleInfo->UniqueProcessId != ProcessId)
				continue;
			if (currentHandleInfo->ObjectTypeIndex != TypeIndex)
				continue;
			if (GrantedAccess != nullptr && *GrantedAccess != currentHandleInfo->GrantedAccess)
				continue;
			handlesVector.push_back(reinterpret_cast<HANDLE>(currentHandleInfo->HandleValue));
		}
	}
	return handlesVector;
}

NTSTATUS NtQueryObjectWrapper(const HANDLE Handle, unique_ptr<BYTE>& ObjInfoBuffer, const OBJECT_INFORMATION_CLASS ObjectInformationClass)
{
	NTSTATUS status = 0;
	ULONG resultLength = 0;

	// Make sure the unique_ptr will be empty if the function fails
	ObjInfoBuffer.reset();

	// NtQueryObject fails to retrieve the required size for ObjectBasicInformation. If this parameter is sent, the buffer must be already allocated
	if (ObjectBasicInformation == ObjectInformationClass)
	{
		ObjInfoBuffer = std::unique_ptr<BYTE>(new BYTE[sizeof(PUBLIC_OBJECT_BASIC_INFORMATION)]);
		resultLength = sizeof(PUBLIC_OBJECT_BASIC_INFORMATION);
	}
	else
	{

		status = NtQueryObject(Handle, ObjectInformationClass, nullptr, 0, &resultLength);
		// The first call shouldn't succeed because the buffer is empty. Should return STATUS_INFO_LENGTH_MISMATCH (0xC0000004)
		if (NT_SUCCESS(status))
			return STATUS_UNSUCCESSFUL;

		ObjInfoBuffer = std::unique_ptr<BYTE>(new BYTE[resultLength]);
	}

	if (!ObjInfoBuffer)
		return STATUS_NO_MEMORY;

	ZeroMemory(ObjInfoBuffer.get(), resultLength);
	status = NtQueryObject(Handle, ObjectInformationClass, ObjInfoBuffer.get(), resultLength, &resultLength);
	if (!NT_SUCCESS(status))
	{
		// Release the allocation because NtQueryObject failed
		ObjInfoBuffer.reset();
	}

	return status;
}

ULONG GetObjectIndex(const HANDLE Handle)
{
	if (IsWindows8OrGreater())
	{
		unique_ptr<BYTE> objTypeInfoBuffer;
		const NTSTATUS status = NtQueryObjectWrapper(Handle, objTypeInfoBuffer, ObjectTypeInformation);
		if (!NT_SUCCESS(status))
			ThrowException("[GetObjectIndex] NtQueryObject failed", status);

		const auto objTypeInfo = reinterpret_cast<POBJECT_TYPE_INFORMATION>(objTypeInfoBuffer.get());
		return objTypeInfo->TypeIndex;
	}

	// Retrieve the handle table of the entire system
	DWORD pid = GetCurrentProcessId();
	unique_ptr<BYTE> handleSnapshotInfoBuffer;
	GetSystemHandleTable(handleSnapshotInfoBuffer);
	const auto handleSnapshotInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(handleSnapshotInfoBuffer.get());

	// Find the handle sent in the global table and return the ObjectTypeIndex
	for (int i = 0; i < handleSnapshotInfo->NumberOfHandles; i++)
	{
		const SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX* currentHandleInfo = &handleSnapshotInfo->Handles[i];
		if (currentHandleInfo->UniqueProcessId == pid && currentHandleInfo->HandleValue == reinterpret_cast<ULONG_PTR>(Handle))
			return currentHandleInfo->ObjectTypeIndex;
	}
	ThrowException("[GetObjectIndex] Object index not found", STATUS_NOT_FOUND);
}

void ThrowException(const char* Message, const DWORD ErrorCode)
{
	std::stringstream messageStream;
	messageStream << Message;
	messageStream << " 0x";
	messageStream << std::hex << ErrorCode << std::dec;
	const string errorMessage = messageStream.str();
	throw runtime_error(errorMessage.c_str());
}

PBYTE GetTokenInfoWrap(const HANDLE TokenHandle, const TOKEN_INFORMATION_CLASS TokenInfoClass)
{
	DWORD tokenInformationSize = 0;
	::GetTokenInformation(TokenHandle, TokenInfoClass, nullptr, 0, &tokenInformationSize);

	// The first call should fail because the buffer pointer is null. It is made to retrieve the required size of the buffer
	const DWORD error = GetLastError();
	if (ERROR_INSUFFICIENT_BUFFER != error)
		ThrowException("[GetTokenInfoWrap] Error getting buffer size from GetTokenInformation", error);

	// Allocate the memory required to store the info
	const auto tokenInfoBuffer = new BYTE[tokenInformationSize];

	// Call GetTokenInformation again with a pointer to a buffer
	if (!::GetTokenInformation(TokenHandle, TokenInfoClass, tokenInfoBuffer, tokenInformationSize, &tokenInformationSize))
		ThrowException("[GetTokenInfoWrap] Error retrieving info from GetTokenInformation", GetLastError());
	return tokenInfoBuffer;
}

bool GetTokenUser(const HANDLE TokenHandle, wstring& User)
{
	const auto tokenInfo = std::unique_ptr<BYTE>(GetTokenInfoWrap(TokenHandle, TokenUser));
	const auto tokenUser = reinterpret_cast<PTOKEN_USER>(tokenInfo.get());

	DWORD usernameSize = 0;
	DWORD domainSize = 0;
	SID_NAME_USE sidNameUse;
	::LookupAccountSidW(nullptr, tokenUser->User.Sid, nullptr, &usernameSize, nullptr, &domainSize, &sidNameUse);

	const auto usernameBuffer = std::unique_ptr<BYTE>(new BYTE[usernameSize * sizeof(TCHAR)]);
	const auto domainBuffer = std::unique_ptr<BYTE>(new BYTE[domainSize * sizeof(TCHAR)]);
	const auto username = reinterpret_cast<LPWSTR>(usernameBuffer.get());
	const auto domain = reinterpret_cast<LPWSTR>(domainBuffer.get());
	if (!::LookupAccountSidW(nullptr, tokenUser->User.Sid, username, &usernameSize, domain, &domainSize, &sidNameUse))
		return false;

	User.assign(domain);
	User.append(L"\\");
	User.append(username);
	return true;
}

bool CompareTokenSid(const HANDLE TokenHandle, const WELL_KNOWN_SID_TYPE WellKnownSid)
{
	const auto tokenInfo = std::unique_ptr<BYTE>(GetTokenInfoWrap(TokenHandle, TokenUser));
	const auto tokenUser = reinterpret_cast<PTOKEN_USER>(tokenInfo.get());
	return IsWellKnownSid(tokenUser->User.Sid, WellKnownSid);
}

bool IsSystemToken(const HANDLE TokenHandle)
{
	return CompareTokenSid(TokenHandle, WinLocalSystemSid);
}

void StringToSockAddr(wstring& AddressString, SOCKADDR* SockAddress)
{
	INT sockaddrSize = sizeof(SOCKADDR);
	WSAStringToAddressW(AddressString.data(), AF_INET, nullptr, SockAddress, &sockaddrSize);
}

DWORD GetServicePidBySession(wstring ServiceName, DWORD SessionId)
{
	DWORD pid = 0;
	SC_HANDLE scmHandle = nullptr;
	do
	{
		// Open a handle to the service manager
		scmHandle = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
		if (!scmHandle)
			ThrowException("[GetServicePidBySession] OpenSCManagerW failed", GetLastError());

		// Get the initial size of memory to allocate
		DWORD bytesNeeded = 0;
		DWORD numOfServices = 0;
		DWORD resumeHandle = 0;
		unique_ptr<BYTE> buffer = nullptr;
		EnumServicesStatusW(scmHandle, SERVICE_WIN32_SHARE_PROCESS, SERVICE_ACTIVE, nullptr, 0, &bytesNeeded, &numOfServices, &resumeHandle);

		do
		{
			// Look for already active services running inside svchost.exe
			buffer = unique_ptr<BYTE>(new BYTE[bytesNeeded]);
			LPENUM_SERVICE_STATUSW servicesArray = reinterpret_cast<LPENUM_SERVICE_STATUSW>(buffer.get());
			if (!EnumServicesStatusW(scmHandle, SERVICE_WIN32_SHARE_PROCESS, SERVICE_ACTIVE, servicesArray, bytesNeeded, &bytesNeeded, &numOfServices, &resumeHandle))
				break;

			for (int i = 0; i < numOfServices; i++)
			{
				LPENUM_SERVICE_STATUSW service = &servicesArray[i];
				wstring currentServiceName = service->lpServiceName;

				// Check the ServiceName is a substring of currentServiceName
				if (currentServiceName.find(ServiceName) != wstring::npos)
				{
					DWORD currentServicePid = 0;
					DWORD currentServiceSessionId = 0;

					// If there is an error getting the pid of the service, skip it
					try
					{
						currentServicePid = GetServicePid(currentServiceName);
					}
					catch (std::exception& ex)
					{
						continue;
					}
					ProcessIdToSessionId(currentServicePid, &currentServiceSessionId);

					if (currentServiceSessionId == SessionId)
					{
						// The process was found
						pid = currentServicePid;
						break;
					}
				}
			}
			buffer.release();
		} while (GetLastError() != ERROR_MORE_DATA && pid != 0);
	} while (false);

	if (scmHandle)
		CloseServiceHandle(scmHandle);

	if (pid)
		return pid;
	ThrowException("[GetServicePidBySession] No service found for this session", STATUS_NOT_FOUND);
}

wstring GetUsernameFromSession(DWORD SessionId)
{
	wstring fullName;
	LPWSTR domainName;
	LPWSTR userName;
	DWORD bufferSize = 0;
	if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, SessionId, WTSDomainName, &domainName, &bufferSize))
		ThrowException("WTSQuerySessionInformationW for WTSDomainName failed", GetLastError());
	DWORD stringLen = wcslen(domainName);
	fullName.assign(domainName, stringLen);
	WTSFreeMemory(domainName);

	bufferSize = 0;
	if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, SessionId, WTSUserName, &userName, &bufferSize))
		ThrowException("WTSQuerySessionInformationW for WTSDomainName failed", GetLastError());

	fullName.append(L"\\");
	stringLen = wcslen(userName);
	fullName.append(userName, stringLen);
	WTSFreeMemory(userName);
	return fullName;
}
