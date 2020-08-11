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
        pDeviceContext->InternalState[14] = 4;
        pDeviceContext->InternalState[10] = 7;
        pDeviceContext->InternalState[2] = 1;
        pDeviceContext->InternalState[18] = 0;
        pDeviceContext->InternalState[6] = 2;

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

        status = WdfDeviceCreateDeviceInterface(
            device,
            &GUID_DEVINTERFACE_Ice5Lp2k,
            NULL // ReferenceString
            );

        if (NT_SUCCESS(status)) {
            //
            // Initialize the I/O Package and any Queues
            //
            status = Ice5Lp2kQueueInitialize(device);
        }
    }

    return status;
}
