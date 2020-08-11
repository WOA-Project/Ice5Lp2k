// Queue.c: driver queue config

#include "driver.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Ice5Lp2kQueueInitialize)
#endif

NTSTATUS
Ice5Lp2kQueueInitialize(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PDEVICE_CONTEXT pDeviceContext;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    pDeviceContext = DeviceGetContext(Device);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
    );

    queueConfig.EvtIoDeviceControl = Ice5Lp2kEvtIoDeviceControl;
    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &pDeviceContext->DefaultIoQueue
                 );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual
    );

    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->DelayedIoCtlQueue
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual
    );

    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->PdReadQueue
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device,
        pDeviceContext->PdReadQueue,
        WdfRequestTypeRead
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfDeviceConfigureRequestDispatching failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchSequential
    );

    queueConfig.PowerManaged = WdfFalse;
    queueConfig.EvtIoWrite = Ice5Lp2kQueueEvtWrite;
    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->PdWriteQueue
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device,
        pDeviceContext->PdWriteQueue,
        WdfRequestTypeWrite
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfDeviceConfigureRequestDispatching failed %!STATUS!", status);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

VOID
Ice5Lp2kEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE Device; // r0
    PDEVICE_CONTEXT pDeviceContext; // r0
    NTSTATUS forwardStatus; // r4

    TraceEvents(TRACE_LEVEL_INFORMATION,
        TRACE_QUEUE,
        "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d",
        Queue, Request, (int)OutputBufferLength, (int)InputBufferLength, IoControlCode);

    Device = WdfIoQueueGetDevice(Queue);
    pDeviceContext = DeviceGetContext(Device);

    if (IoControlCode != 0x22C006)
    {
        WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, 0);
    }
        
    if (IoControlCode > 0x22C015)
    {
        switch (IoControlCode)
        {
        case 0x22C019u:
            UC120IoctlEnableGoodCRC(pDeviceContext, Request);
            break;
        case 0x22C01Cu:
            UC120IoctlExecuteHardReset(pDeviceContext, Request);
            goto LABEL_62;
        case 0x83203E84:
            // Not going to support mfg calibration write request
            WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
            goto LABEL_62;
        }
        goto LABEL_61;
    }
    if (IoControlCode == 0x22C015)
    {
        UC120IoctlSetVConnRoleSwitch(pDeviceContext, Request);
        goto LABEL_62;
    }
    if (IoControlCode != 0x22C006)
    {
        switch (IoControlCode)
        {
        case 0x22C00Au:
            UC120IoctlIsCableConnected(pDeviceContext, Request);
            goto LABEL_62;
        case 0x22C00Du:
            UC120IoctlReportNewDataRole(pDeviceContext, Request);
            goto LABEL_62;
        case 0x22C011u:
            UC120IoctlReportNewPowerRole(pDeviceContext, Request);
        LABEL_62:
            WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
            return;
        }
    LABEL_61:
        if (IoControlCode == 0x22C006)
            return;
        goto LABEL_62;
    }

    forwardStatus = WdfRequestForwardToIoQueue(Request, pDeviceContext->DelayedIoCtlQueue);
    if (!NT_SUCCESS(forwardStatus))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestForwardToIoQueue failed %!STATUS!", forwardStatus);
        WdfRequestComplete(Request, forwardStatus);
    }
}

void Ice5Lp2kQueueEvtWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    WDFDEVICE Device; // r0
    PDEVICE_CONTEXT pDeviceContext; // r0
    NTSTATUS status; // r4
    UCHAR s22; // r3
    PVOID Content; // [sp+8h] [bp-30h]
    LARGE_INTEGER WaitTime; // [sp+10h] [bp-28h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    Device = WdfIoQueueGetDevice(Queue);
    pDeviceContext = DeviceGetContext(Device);
    if (!Length)
    {
        status = 0xC000000D;
        goto LABEL_11;
    }

    pDeviceContext->InternalState[22] = 0xFFu;
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
        status = 0xC00000B5;
        goto LABEL_46;
    }

    s22 = pDeviceContext->InternalState[22];
    if (pDeviceContext->InternalState[22])
    {
        switch (s22)
        {
        case 1:
            status = 0xC00000AE;
            goto LABEL_46;
        case 2:
            status = 0xC00000B5;
            goto LABEL_46;
        case 6:
            status = 0xC00002CA;
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