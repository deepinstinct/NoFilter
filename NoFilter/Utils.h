#pragma once
#include <winsock2.h>
#include <ws2ipdef.h>
#include <mstcpip.h>
#include <Ws2tcpip.h>
#include <versionhelpers.h>
#include "ntddk.h"
#include <stdexcept>
#include <memory>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <wtsapi32.h>

using std::runtime_error;
using std::string;
using std::stringstream;
using std::wstring;
using std::unique_ptr;
using std::vector;
using std::wcout;
using std::endl;

wstring GetPathFromHandle(HANDLE ObjectHandle);
bool LaunchCMD(HANDLE TokenHandle);
void EnableDebugPrivilegeCurrentProcess();
DWORD GetServicePid(wstring ServiceName);
vector<HANDLE> GetHandlesFromProcess(DWORD ProcessId, ULONG TypeIndex, PACCESS_MASK GrantedAccess);
ULONG GetObjectIndex(HANDLE Handle);
void ThrowException(const char* Message, DWORD ErrorCode);
bool GetTokenUser(HANDLE TokenHandle, wstring& User);
bool IsSystemToken(HANDLE TokenHandle);
void StringToSockAddr(wstring& AddressString, SOCKADDR* SockAddress);
DWORD GetServicePidBySession(wstring ServiceName, DWORD SessionId);
wstring GetUsernameFromSession(DWORD SessionId);