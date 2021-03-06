/*++

Module Name:

    driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <spb.h>

#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>

#include "device.h"
#include "queue.h"
#include "trace.h"

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Ice5Lp2kEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP Ice5Lp2kEvtDriverContextCleanup;

EXTERN_C_END
