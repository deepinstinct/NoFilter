

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0626 */
/* at Tue Jan 19 05:14:07 2038
 */
/* Compiler settings for SyncController.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0626 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __SyncController_h_h__
#define __SyncController_h_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if _CONTROL_FLOW_GUARD_XFG
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

/* header files for imported files */
#include "wtypesbase.h"

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __SyncController_INTERFACE_DEFINED__
#define __SyncController_INTERFACE_DEFINED__

/* interface SyncController */
/* [version][uuid] */ 

int AccountsMgmtRpcCreateAccount( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcDeleteAccount( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcConvertWebAccountIdFromAppSpecificId( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcConvertWebAccountIdToAppSpecificId( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcSyncAccount( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcSyncAccountAndWaitForCompletion( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcQueryAccountProperties( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcSaveAccountProperties( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcEnumAccounts( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcAdviseAccount( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcUnadviseAccount( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcGetNotifications( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcDiscoverExchangeServerConfig( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcDiscoverExchangeServerAuthType( 
    /* [in] */ handle_t IDL_handle,
    /* [string][in] */ const wchar_t *ServerAddress,
    /* [out] */ int *IntOut);

int AccountsMgmtRpcVerifyExchangeMailBoxTokenAuth( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcDiscoverInternetMailServerConfig( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcCancelDiscoverInternetMailServerConfig( 
    /* [in] */ handle_t IDL_handle);

int AccountsMgmtRpcMayIgnoreInvalidServerCertificate( 
    /* [in] */ handle_t IDL_handle);



extern RPC_IF_HANDLE SyncController_v1_0_c_ifspec;
extern RPC_IF_HANDLE SyncController_v1_0_s_ifspec;
#endif /* __SyncController_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


