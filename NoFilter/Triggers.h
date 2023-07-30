#pragma once
#include "Consts.h"
#include "ms-rprn_h.h"
#include "SyncController_h.h"

constexpr DWORD THREAD_WAIT_TIME = 5000;

wstring GetEndpointFromHandleTable(const DWORD Pid);
DWORD TriggerPrintSpooler(WfpClient* Client);
DWORD TriggerAccountsMgmt(WfpClient* Client, wstring& Endpoint);