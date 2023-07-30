#pragma once
#include "Utils.h"
#include <fwpmu.h>

typedef struct FWPS_ALE_ENDPOINT_PROPERTIES0_ {
    UINT64         endpointId;
    FWP_IP_VERSION ipVersion;
    union {
        UINT32 localV4Address;
        UINT8  localV6Address[16];
    };
    union {
        UINT32 remoteV4Address;
        UINT8  remoteV6Address[16];
    };
    UINT8          ipProtocol;
    UINT16         localPort;
    UINT16         remotePort;
    UINT64         localTokenModifiedId;
    UINT64         mmSaId;
    UINT64         qmSaId;
    UINT32         ipsecStatus;
    UINT32         flags;
    FWP_BYTE_BLOB  appId;
} FWPS_ALE_ENDPOINT_PROPERTIES0;

typedef struct FWPS_ALE_ENDPOINT_ENUM_TEMPLATE0_ {
    FWP_CONDITION_VALUE0 localSubNet;
    FWP_CONDITION_VALUE0 remoteSubNet;
    FWP_CONDITION_VALUE0 ipProtocol;
    FWP_CONDITION_VALUE0 localPort;
    FWP_CONDITION_VALUE0 remotePort;
} FWPS_ALE_ENDPOINT_ENUM_TEMPLATE0;

typedef NTSTATUS(NTAPI* FwpsOpenToken0)
(
    IN HANDLE engineHandle,
    IN LUID modifiedId,
    IN DWORD desiredAccess,
    OUT HANDLE* accessToken
);

typedef NTSTATUS(NTAPI* FwpsAleEndpointCreateEnumHandle0)
(
    IN           HANDLE                                 engineHandle,
    IN OPTIONAL const FWPS_ALE_ENDPOINT_ENUM_TEMPLATE0* enumTemplate,
    OUT          HANDLE* enumHandle
);

typedef NTSTATUS(NTAPI* FwpsAleEndpointEnum0)
(
    IN  HANDLE                        engineHandle,
    IN  HANDLE                        enumHandle,
    IN  UINT32                        numEntriesRequested,
    OUT FWPS_ALE_ENDPOINT_PROPERTIES0*** entries,
    OUT UINT32* numEntriesReturned
);

typedef NTSTATUS(NTAPI* FwpsAleEndpointDestroyEnumHandle0)
(
    IN      HANDLE engineHandle,
    IN OUT HANDLE enumHandle
);

typedef NTSTATUS(NTAPI* FwpsAleExplicitCredentialsQuery0)
(
    IN HANDLE EngineHandle,
    IN UINT64 ProcessId,
    IN void*** CredentialsArray
    );

typedef struct SetTokenStruct {
    ULONG_PTR Pid;
    HANDLE Token;
};

class WfpClient
{
public:
    WfpClient(DWORD SessionId);
    ~WfpClient();
    DWORD InstallIPSecPolicy(wstring PolicyName, GUID* ProviderKey, wstring LocalAddress, wstring RemoteAddress, wstring PreSharedKey);
    wstring GetIPSecPolicyGuid();
    HANDLE WfpDuplicateToken(DWORD Pid, HANDLE TokenHandle) const;
    HANDLE BruteForceTable();

private:
    LUID SetToken(DWORD Pid, HANDLE TokenHandle) const;
    HANDLE GetToken(LUID Luid) const;
    void ReleaseToken(LUID Luid) const;
    static HANDLE GetWfpAleHandle();
    static DWORD GetBfePid();
    static HANDLE FindFileHandle(const DWORD ProcessId, const wstring& FilePath);
    static ULONG GetFileObjectIndex();
    HANDLE QueryBfe(LUID Luid);
    void GenerateBlob(wstring PreSharedKey, FWP_BYTE_BLOB** Blob);
    NTSTATUS QueryCredentials(UINT64 Input);
    HANDLE m_EngineHandle;
    HANDLE m_DeviceHandle;
    UUID m_IPSecPolicyGuid;
    wstring m_TargetUsername;
    FwpsOpenToken0 m_FwpsOpenToken0;
};