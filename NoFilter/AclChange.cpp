#include "AclChange.h"

// Based on https://github.com/OmriBaso/BesoToken

void ChangeDacl(const HWINSTA WindowsStationHandle, const HDESK DesktopHandle)
{
    DWORD bytesNeeded = 0;
    CreateWellKnownSid(WinWorldSid, nullptr, nullptr, &bytesNeeded);
    const auto everyoneSid = std::unique_ptr<BYTE>(new BYTE[bytesNeeded]);
    if (!CreateWellKnownSid(WinWorldSid, nullptr, everyoneSid.get(), &bytesNeeded))
        ThrowException("CreateWellKnownSid for WinWorldSid failed", GetLastError());

    bytesNeeded = 0;
    CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, nullptr, &bytesNeeded);
    const auto  allAppContainersSid = std::unique_ptr<BYTE>(new BYTE[bytesNeeded]);
    if (!CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, allAppContainersSid.get(), &bytesNeeded))
        ThrowException("CreateWellKnownSid for WinBuiltinAnyPackageSid failed", GetLastError());

    constexpr int numOfAces = 2;

    EXPLICIT_ACCESS WorkstationEA[numOfAces] = {};
    TRUSTEE WorkstationTrutEE[numOfAces] = {};

    WorkstationTrutEE[0].pMultipleTrustee = nullptr;
    WorkstationTrutEE[0].MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    WorkstationTrutEE[0].TrusteeForm = TRUSTEE_IS_SID;
    WorkstationTrutEE[0].TrusteeType = TRUSTEE_IS_UNKNOWN;
    WorkstationTrutEE[0].ptstrName = reinterpret_cast<LPWCH>(everyoneSid.get());

    WorkstationEA[0].grfAccessPermissions = GENERIC_ACCESS;
    WorkstationEA[0].grfAccessMode = GRANT_ACCESS;
    WorkstationEA[0].grfInheritance = NULL;
    WorkstationEA[0].Trustee = WorkstationTrutEE[0];


    WorkstationTrutEE[1].pMultipleTrustee = nullptr;
    WorkstationTrutEE[1].MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    WorkstationTrutEE[1].TrusteeForm = TRUSTEE_IS_SID;
    WorkstationTrutEE[1].TrusteeType = TRUSTEE_IS_UNKNOWN;
    WorkstationTrutEE[1].ptstrName = reinterpret_cast<LPWCH>(allAppContainersSid.get());


    WorkstationEA[1].grfAccessPermissions = GENERIC_ACCESS;
    WorkstationEA[1].grfAccessMode = GRANT_ACCESS;
    WorkstationEA[1].grfInheritance = NULL;
    WorkstationEA[1].Trustee = WorkstationTrutEE[1];


    EXPLICIT_ACCESS DesktopEA[numOfAces] = {};
    TRUSTEE DesktopTrutEE[numOfAces] = {};

    DesktopTrutEE[0].pMultipleTrustee = nullptr;
    DesktopTrutEE[0].MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    DesktopTrutEE[0].TrusteeForm = TRUSTEE_IS_SID;
    DesktopTrutEE[0].TrusteeType = TRUSTEE_IS_UNKNOWN;
    DesktopTrutEE[0].ptstrName = reinterpret_cast<LPWCH>(everyoneSid.get());

    DesktopEA[0].grfAccessPermissions = GENERIC_ACCESS;
    DesktopEA[0].grfAccessMode = GRANT_ACCESS;
    DesktopEA[0].grfInheritance = NULL;
    DesktopEA[0].Trustee = DesktopTrutEE[0];


    DesktopTrutEE[1].pMultipleTrustee = nullptr;
    DesktopTrutEE[1].MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    DesktopTrutEE[1].TrusteeForm = TRUSTEE_IS_SID;
    DesktopTrutEE[1].TrusteeType = TRUSTEE_IS_UNKNOWN;
    DesktopTrutEE[1].ptstrName = reinterpret_cast<LPWCH>(allAppContainersSid.get());

    DesktopEA[1].grfAccessPermissions = GENERIC_ACCESS;
    DesktopEA[1].grfAccessMode = GRANT_ACCESS;
    DesktopEA[1].grfInheritance = 0U;
    DesktopEA[1].Trustee = DesktopTrutEE[1];

    const PACL oldAcl = nullptr;
    PACL newWindowsStationAcl = nullptr;
    PACL newDesktopAcl = nullptr;

    if (SetEntriesInAclW(numOfAces, WorkstationEA, oldAcl, &newWindowsStationAcl))
        ThrowException("SetEntriesInAclW for windows station failed", GetLastError());

    if (SetEntriesInAclW(numOfAces, DesktopEA, oldAcl, &newDesktopAcl))
    {
        LocalFree(newWindowsStationAcl);
        ThrowException("SetEntriesInAclW for desktop failed", GetLastError());
    }

    if (SetSecurityInfo(WindowsStationHandle, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newWindowsStationAcl, nullptr))
    {
        LocalFree(newWindowsStationAcl);
        LocalFree(newDesktopAcl);
        ThrowException("SetSecurityInfo for windows station failed", GetLastError());
    }

    if (SetSecurityInfo(DesktopHandle, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDesktopAcl, nullptr))
    {
        LocalFree(newWindowsStationAcl);
        LocalFree(newDesktopAcl);
        ThrowException("SetSecurityInfo for desktop failed", GetLastError());
    }

    LocalFree(newWindowsStationAcl);
    LocalFree(newDesktopAcl);
}


void AdjustDesktop() {

    HWINSTA  winStationHandle = nullptr;
    HDESK desktopHandle = nullptr;

    winStationHandle = OpenWindowStationW(L"winsta0", FALSE, READ_CONTROL | WRITE_DAC);
    if (!winStationHandle)
        ThrowException("OpenWindowStationW failed", GetLastError());

    desktopHandle = OpenDesktopW(L"default", 0, FALSE, READ_CONTROL | WRITE_DAC);
    if (!desktopHandle)
    {
        CloseWindowStation(winStationHandle);
        ThrowException("OpenDesktopW failed", GetLastError());
    }

    ChangeDacl(winStationHandle, desktopHandle);
    CloseWindowStation(winStationHandle);
    CloseDesktop(desktopHandle);
}