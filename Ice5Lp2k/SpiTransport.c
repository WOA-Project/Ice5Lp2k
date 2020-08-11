// SpiTransport.c: SPI utility for talking with UC120

#include <driver.h>
#include "SpiTransport.tmh"

NTSTATUS UC120SpiRead(
    _In_ PSPI_DEVICE_CONNECTION Connection,
    _In_ UCHAR RegisterAddr, _Out_ PUCHAR RegisterValue, _In_ size_t Size
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    command = (UCHAR)(RegisterAddr << 3);

    WDFMEMORY SpbTransferOutputMemory;
    size_t     SpbTransferOutputMemorySize = 2 + Size;
    UCHAR* pSpbTransferOutputMemory = NULL;

    WDFMEMORY SpbTransferInputMemory;
    size_t     SpbTransferInputMemorySize = 1;
    UCHAR* pSpbTransferInputMemory = NULL;

    WDF_MEMORY_DESCRIPTOR SpbTransferListMemoryDescriptor;
    WDF_REQUEST_SEND_OPTIONS RequestOptions;

    LARGE_INTEGER Interval;
    int         Retries = 3;

    // One register write and read
    SPB_TRANSFER_LIST_AND_ENTRIES(2) Sequence;
    SPB_TRANSFER_LIST_INIT(&(Sequence.List), 2);

    // Allocate the memory that holds output buffer
    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES, NonPagedPoolNx, '12CU',
        SpbTransferInputMemorySize, &SpbTransferInputMemory,
        &pSpbTransferInputMemory
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfMemoryCreate failed 0x%x", status);
        goto exit;
    }

    // Allocate the memory that holds output buffer
    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES, NonPagedPoolNx, '12CU',
        SpbTransferOutputMemorySize, &SpbTransferOutputMemory,
        &pSpbTransferOutputMemory);

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfMemoryCreate failed 0x%x", status);
        WdfObjectDelete(SpbTransferInputMemory);
        goto exit;
    }

    RtlCopyMemory(pSpbTransferInputMemory, &command, 1);

    {
        //
        // PreFAST cannot figure out the SPB_TRANSFER_LIST_ENTRY
        // "struct hack" size but using an index variable quiets
        // the warning. This is a false positive from OACR.
        //

        Sequence.List.Transfers[0] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(
            SpbTransferDirectionToDevice, 0, pSpbTransferInputMemory,
            (ULONG)SpbTransferInputMemorySize);

        Sequence.List.Transfers[1] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(
            SpbTransferDirectionFromDevice, 0, pSpbTransferOutputMemory,
            (ULONG)SpbTransferOutputMemorySize);
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &SpbTransferListMemoryDescriptor, (PVOID)&Sequence, sizeof(Sequence));

    WDF_REQUEST_SEND_OPTIONS_INIT(
        &RequestOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    RequestOptions.Timeout = WDF_REL_TIMEOUT_IN_MS(561);

    Interval.QuadPart = 0xFFFFFFFFFFFFD8F0;

    do {
        status = WdfIoTargetSendIoctlSynchronously(
            Connection->SpiDeviceIoTarget, NULL, IOCTL_SPB_FULL_DUPLEX,
            &SpbTransferListMemoryDescriptor, NULL, &RequestOptions, NULL
        );

        Retries--;
    } while ((!NT_SUCCESS(status)) && Retries >= 0);

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, TRACE_DRIVER,
            "WdfIoTargetSendIoctlSynchronously failed 0x%x", status);
        WdfObjectDelete(SpbTransferInputMemory);
        WdfObjectDelete(SpbTransferOutputMemory);
        goto exit;
    }

    RtlCopyMemory((PVOID) RegisterValue, pSpbTransferOutputMemory + 2, Size);
    WdfObjectDelete(SpbTransferInputMemory);
    WdfObjectDelete(SpbTransferOutputMemory);

exit:
    return status;
}

NTSTATUS UC120SpiWrite(
    _In_ PSPI_DEVICE_CONNECTION Connection,
    _In_ UCHAR RegisterAddr, _In_ PVOID Content, _In_ size_t Size
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR    Command = (UCHAR)((RegisterAddr << 3) | 1);

    WDFMEMORY                SpbTransferOutputMemory;
    WDF_MEMORY_DESCRIPTOR    SpbTransferOutputMemoryDescriptor;
    size_t                   SpbTransferOutputMemorySize = 1 + Size;

    UCHAR* pSpbTransferOutputMemory = NULL;
    WDF_REQUEST_SEND_OPTIONS RequestOptions;

    // Allocate the memory that holds output buffer
    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES, NonPagedPoolNx, '12CU',
        SpbTransferOutputMemorySize, &SpbTransferOutputMemory,
        &pSpbTransferOutputMemory);

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfMemoryCreate failed 0x%x", status);
        goto exit;
    }

    RtlZeroMemory(pSpbTransferOutputMemory, SpbTransferOutputMemorySize);
    RtlCopyMemory(pSpbTransferOutputMemory, &Command, 1);
    RtlCopyMemory(pSpbTransferOutputMemory + 1, Content, Size);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(
        &SpbTransferOutputMemoryDescriptor, SpbTransferOutputMemory, 0);

    WDF_REQUEST_SEND_OPTIONS_INIT(
        &RequestOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    RequestOptions.Timeout = WDF_REL_TIMEOUT_IN_MS(561);

    status = WdfIoTargetSendWriteSynchronously(
        Connection->SpiDeviceIoTarget, NULL, &SpbTransferOutputMemoryDescriptor, NULL,
        &RequestOptions, NULL);

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_ERROR, TRACE_DRIVER,
            "WdfIoTargetSendWriteSynchronously failed 0x%x", status);
        WdfObjectDelete(SpbTransferOutputMemory);
        goto exit;
    }

exit:
    return status;
}
