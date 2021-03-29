// Queue.c: driver queue config

#include "driver.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Ice5Lp2kQueueInitialize)
#endif

NTSTATUS
Ice5Lp2kQueueInitialize(
    _In_ WDFDEVICE Device)
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PDEVICE_CONTEXT pDeviceContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    pDeviceContext = DeviceGetContext(Device);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential);

    queueConfig.EvtIoDeviceControl = Ice5Lp2kEvtIoDeviceControl;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->DefaultIoQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->DelayedIoCtlQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->PdReadQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device,
        pDeviceContext->PdReadQueue,
        WdfRequestTypeRead);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfDeviceConfigureRequestDispatching failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchSequential);

    queueConfig.PowerManaged = WdfFalse;
    queueConfig.EvtIoWrite = Ice5Lp2kQueueEvtWrite;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->PdWriteQueue);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device,
        pDeviceContext->PdWriteQueue,
        WdfRequestTypeWrite);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfDeviceConfigureRequestDispatching failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

VOID Ice5Lp2kEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT pDeviceContext;
    NTSTATUS forwardStatus;

    TraceEvents(TRACE_LEVEL_INFORMATION,
                TRACE_QUEUE,
                "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d",
                Queue, Request, (int)OutputBufferLength, (int)InputBufferLength, IoControlCode);

    Device = WdfIoQueueGetDevice(Queue);
    pDeviceContext = DeviceGetContext(Device);

    if (IoControlCode == IOCTL_UC120_NOTIFICATION)
    {
        forwardStatus = WdfRequestForwardToIoQueue(Request, pDeviceContext->DelayedIoCtlQueue);
        if (!NT_SUCCESS(forwardStatus))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestForwardToIoQueue failed %!STATUS!", forwardStatus);
            WdfRequestComplete(Request, forwardStatus);
        }
        return;
    }

    WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, 0);

    switch (IoControlCode)
    {
    case IOCTL_UC120_GET_CABLE_DETECTION_STATE:
        UC120GetCableDetectionState(pDeviceContext, Request);
        break;
    case IOCTL_UC120_SET_PORT_DATA_ROLE:
        UC120SetPortDataRole(pDeviceContext, Request);
        break;
    case IOCTL_UC120_SET_PORT_POWER_ROLE:
        UC120IoctlReportNewPowerRole(pDeviceContext, Request);
        break;
    case IOCTL_UC120_SET_PORT_VCONN_ROLE:
        UC120IoctlSetVConnRoleSwitch(pDeviceContext, Request);
        break;
    case IOCTL_UC120_PD_MESSAGING_ENABLE:
        UC120PDMessagingEnable(pDeviceContext, Request);
        break;
    case IOCTL_UC120_SEND_HARD_RESET:
        UC120IoctlExecuteHardReset(pDeviceContext, Request);
        break;
    case 0x83203E84:
    default:
        // Not going to support mfg calibration write request
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        break;
    }

    WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
}

void Ice5Lp2kQueueEvtWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT pDeviceContext;
    NTSTATUS status;
    PVOID Content;
    LARGE_INTEGER WaitTime;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    Device = WdfIoQueueGetDevice(Queue);
    pDeviceContext = DeviceGetContext(Device);
    if (!Length)
    {
        status = 0xC000000D;
        goto LABEL_11;
    }

    pDeviceContext->PDMessageType = 0xFFu;
    KeClearEvent(&pDeviceContext->PdEvent);

    status = WdfRequestRetrieveInputBuffer(Request, Length, &Content, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
        goto LABEL_17;
    }

    WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, 0);

    status = UC120SpiWrite(&pDeviceContext->SpiDevice, 16, Content, Length);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "UC120SpiWrite failed %!STATUS!", status);
        goto LABEL_17;
    }

    status = UC120SpiWrite(&pDeviceContext->SpiDevice, 0, &Length, 1);
    if (!NT_SUCCESS(status))
    {
    LABEL_17:
    LABEL_46:
    LABEL_11:
        WdfRequestComplete(Request, status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return;
    }

    WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);

    WaitTime.QuadPart = 0xFFFFFFFFFFFCF2C0;
    status = KeWaitForSingleObject(&pDeviceContext->PdEvent, 0, 0, 0, &WaitTime);

    if (status == 258)
    {
        status = STATUS_IO_TIMEOUT;
        goto LABEL_46;
    }

    if (pDeviceContext->PDMessageType)
    {
        switch (pDeviceContext->PDMessageType)
        {
        case 1:
            status = STATUS_PIPE_BUSY;
            goto LABEL_46;
        case 2:
            status = STATUS_IO_TIMEOUT;
            goto LABEL_46;
        case 6:
            status = STATUS_TRANSPORT_FULL;
            goto LABEL_46;
        }
    }
    else
    {
        status = 0;
    }

    if (status < 0)
    {
        goto LABEL_46;
    }

    WdfRequestCompleteWithInformation(Request, status, Length);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}