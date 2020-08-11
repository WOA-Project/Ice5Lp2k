// Device.c: Device event handler

#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Ice5Lp2kCreateDevice)
#endif

NTSTATUS
Ice5Lp2kCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    PDEVICE_CONTEXT pDeviceContext;
    WDFDEVICE device;
    NTSTATUS status;

    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_INTERRUPT_CONFIG interruptConfig;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = UC120EvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = UC120EvtDeviceD0Entry;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (NT_SUCCESS(status)) {
        pDeviceContext = DeviceGetContext(device);

        pDeviceContext->Device = device;
        // I don't know what they do. Leave them as is.
        pDeviceContext->InternalState[2] = 1;
        pDeviceContext->InternalState[6] = 2;
        pDeviceContext->InternalState[10] = 7;
        pDeviceContext->InternalState[14] = 4;
        pDeviceContext->InternalState[18] = 0;
        
        // Initialize interrupts
        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, UC120InterruptIsr, NULL);
        interruptConfig.PassiveHandling = TRUE;
        interruptConfig.EvtInterruptEnable = UC120InterruptEnable;
        interruptConfig.EvtInterruptDisable = UC120InterruptDisable;
        status = WdfInterruptCreate(
            device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDeviceContext->Uc120Interrupt
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfInterruptCreate failed %!STATUS!", status);
            return status;
        }

        WdfInterruptSetPolicy(
            pDeviceContext->Uc120Interrupt,
            WdfIrqPolicyAllProcessorsInMachine,
            WdfIrqPriorityHigh,
            0
        );

        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, PlugdetInterruptIsr, NULL);
        interruptConfig.PassiveHandling = TRUE;
        status = WdfInterruptCreate(
            device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDeviceContext->PlugDetInterrupt
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfInterruptCreate failed %!STATUS!", status);
            return status;
        }

        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, PmicInterrupt1Isr, NULL);
        interruptConfig.PassiveHandling = TRUE;
        interruptConfig.EvtInterruptWorkItem = PmicInterrupt1WorkItem;
        status = WdfInterruptCreate(
            device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDeviceContext->Pmic1Interrupt
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfInterruptCreate failed %!STATUS!", status);
            return status;
        }

        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, PmicInterrupt2Isr, NULL);
        interruptConfig.PassiveHandling = TRUE;
        status = WdfInterruptCreate(
            device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDeviceContext->Pmic2Interupt
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfInterruptCreate failed %!STATUS!", status);
            return status;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
        objectAttributes.ParentObject = device;
        status = WdfCollectionCreate(
            &objectAttributes,
            &pDeviceContext->DelayedQueryIoctlRequestCol
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfCollectionCreate failed %!STATUS!", status);
            return status;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
        objectAttributes.ParentObject = device;
        status = WdfWaitLockCreate(
            &objectAttributes,
            &pDeviceContext->DeviceWaitLock
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfWaitLockCreate failed %!STATUS!", status);
            return status;
        }

        //
        // Initialize the I/O Package and any Queues
        //
        status = Ice5Lp2kQueueInitialize(device);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS
UC120AcquireInitializeResourcesFromAcpi(
    PDEVICE_CONTEXT DeviceContext, WDFCMRESLIST ResourcesTranslated
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDescriptor = NULL;
    UCHAR SpiFound = FALSE;
    ULONG ResourceCount;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    ResourceCount = WdfCmResourceListGetCount(ResourcesTranslated);
    for (ULONG i = 0; i < ResourceCount; i++) {
        ResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

        switch (ResDescriptor->Type) {
        case CmResourceTypeConnection:
            // Check for SPI resource
            if (ResDescriptor->u.Connection.Class ==
                CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
                ResDescriptor->u.Connection.Type ==
                CM_RESOURCE_CONNECTION_TYPE_SERIAL_SPI) {

                DeviceContext->SpiDevice.SpiDeviceIdLow = ResDescriptor->u.Connection.IdLowPart;
                DeviceContext->SpiDevice.SpiDeviceIdHigh = ResDescriptor->u.Connection.IdHighPart;
                SpiFound = TRUE;
            }
            break;
        default:
            // We don't care about other descriptors.
            break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return Status;
}

NTSTATUS UC120OpenResources(
    WDFDEVICE Device, PSPI_DEVICE_CONNECTION ConnectionInfo
)
{
    NTSTATUS                  status = STATUS_SUCCESS;
    WDF_IO_TARGET_OPEN_PARAMS OpenParams;
    UNICODE_STRING            ReadString;
    WCHAR                     ReadStringBuffer[260];

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    RtlInitEmptyUnicodeString(
        &ReadString, ReadStringBuffer, sizeof(ReadStringBuffer));

    status =
        RESOURCE_HUB_CREATE_PATH_FROM_ID(&ReadString,
            ConnectionInfo->SpiDeviceIdLow,
            ConnectionInfo->SpiDeviceIdHigh
        );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "RESOURCE_HUB_CREATE_PATH_FROM_ID failed 0x%x", status);
        goto Exit;
    }

    status = WdfIoTargetCreate(Device, WDF_NO_OBJECT_ATTRIBUTES, &ConnectionInfo->SpiDeviceIoTarget);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfIoTargetCreate failed 0x%x", status);
        goto Exit;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&OpenParams, &ReadString, GENERIC_READ | GENERIC_WRITE);
    status = WdfIoTargetOpen(ConnectionInfo->SpiDeviceIoTarget, &OpenParams);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfIoTargetOpen failed 0x%x", status);
        goto Exit;
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS UC120EvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated)
{
    PDEVICE_CONTEXT pDeviceContext; // r7
    NTSTATUS status; // r4

    UNREFERENCED_PARAMETER(ResourcesRaw);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    pDeviceContext = DeviceGetContext(Device);
    status = UC120AcquireInitializeResourcesFromAcpi(pDeviceContext, ResourcesTranslated);
    if (NT_SUCCESS(status))
    {
        status = UC120OpenResources(Device, &pDeviceContext->SpiDevice);
        if (NT_SUCCESS(status))
        {
            status = WdfDeviceCreateDeviceInterface(
                Device,
                &GUID_DEVINTERFACE_Ice5Lp2k,
                NULL // ReferenceString
            );
            if (!NT_SUCCESS(status))
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDeviceCreateDeviceInterface failed 0x%x",status);
            }
            KeInitializeEvent(&pDeviceContext->PdEvent, NotificationEvent, 0);
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "UC120OpenResources failed 0x%x", status);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "Uc120AcquireInitializeResourcesFromAcpi failed 0x%x", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS UC120EvtDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    PDEVICE_CONTEXT pDeviceContext; // r6
    NTSTATUS status; // r5

    UNREFERENCED_PARAMETER(PreviousState);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    pDeviceContext = DeviceGetContext(Device);
    pDeviceContext->Register4 |= 6u;
    status = UC120SpiWrite(&pDeviceContext->SpiDevice, 4, &pDeviceContext->Register4, 1);
    if (NT_SUCCESS(status))
    {
        pDeviceContext->Register5 = 0x88u;
        status = UC120SpiWrite(&pDeviceContext->SpiDevice, 5, &pDeviceContext->Register5, 1);
        if (NT_SUCCESS(status))
        {
            pDeviceContext->Register13 = pDeviceContext->Register13 & 0xFC | 2;
            status = UC120SpiWrite(&pDeviceContext->SpiDevice, 13, &pDeviceContext->Register13, 1);
            if (NT_SUCCESS(status))
            {
                status = UC120Calibrate(pDeviceContext);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "UC120Calibrate failed 0x%x", status);
                }
                pDeviceContext->Calibrated = 1;
            }
            else
            {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "UC120SpiWrite failed 0x%x", status);
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "UC120SpiWrite failed 0x%x", status);
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "UC120SpiWrite failed 0x%x", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}
