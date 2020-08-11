// Interrupt.c: interrupt handler routine

#include <Driver.h>
#include "Interrupt.tmh"

BOOLEAN UC120InterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageID)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT pDeviceContext;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(MessageID);

    Device = WdfInterruptGetDevice(Interrupt);
    pDeviceContext = DeviceGetContext(Device);

    WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, NULL);

    status = UC120SpiRead(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
        pDeviceContext->Register2 = 0xff;
    }
    else {
        UC120InterruptIsrInternal(pDeviceContext);
    }

    pDeviceContext->Register2 = 0xFFu;
    status = UC120SpiWrite(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
    }

    WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
    return TRUE;
}

void UC120InterruptIsrInternal(PDEVICE_CONTEXT DeviceContext)
{
    NTSTATUS status; // r0
    UCHAR Role; // r7
    UCHAR Reg7Value; // r1
    UCHAR Type; // r8
    UCHAR CablePolarity; // r3
    UCHAR Polarity; // r10
    UCHAR CableType; // r3
    UCHAR Power; // r7
    UCHAR Register2Bits; // r1
    UCHAR StateIndex; // r6

    if (DeviceContext->Register2 & 0xFC)
    {
        status = UC120SpiRead(&DeviceContext->SpiDevice, 7, &DeviceContext->Register7, 1u);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
            return;
        }

        Role = (DeviceContext->Register2 & 0x3Cu) >> 2;
        if (Role)
        {
            Reg7Value = DeviceContext->Register7;
            Type = 3;

            if (Reg7Value & 0x40) {
                CablePolarity = 2;
            }
            else {
                CablePolarity = 1;
            }

            Polarity = CablePolarity;
            CableType = (Reg7Value >> 4) & 3;

            if (CableType)
            {
                switch (CableType)
                {
                case 1:
                    Type = 0;
                    break;
                case 2:
                    Type = 1;
                    break;
                case 3:
                    Type = 2;
                    break;
                }
            }
            else
            {
                Type = 3;
            }

            switch (Role)
            {
            case 1u:
            case 4u:
                if (DeviceContext->InternalState[2] != 1)
                {
                    // sub_406D0C(v1, 1, 2, 7);
                    UC120ReportState(DeviceContext, 1, 2, 7, 4, 0);
                    DeviceContext->InternalState[6] = 2;
                    DeviceContext->InternalState[10] = 7;
                    DeviceContext->InternalState[2] = 1;
                    DeviceContext->InternalState[18] = 0;
                    DeviceContext->InternalState[14] = 4;
                    UC120ToggleReg4YetUnknown(DeviceContext, 1);
                    SetVConn(DeviceContext, 0);
                    SetPowerRole(DeviceContext, 0);
                }
                break;
            case 2u:
                Power = 0;
                Type = 1;
                goto LABEL_40;
            case 3u:
                Power = 0;
                Type = 3;
                goto LABEL_40;
            case 5u:
                Type = 0;
                Power = 1;
                goto LABEL_41;
            case 6u:
                Type = 5;
                goto LABEL_37;
            case 7u:
                Type = 4;
            LABEL_37:
                Power = 0;
                Polarity = 0;
                goto LABEL_41;
            case 8u:
                Power = 1;
                Type = 6;
            LABEL_40:
                SetVConn(DeviceContext, 1);
                SetPowerRole(DeviceContext, 1);
            LABEL_41:
                if (DeviceContext->InternalState[2])
                {
                    // sub_406D0C(v1, 0, v12, v8);
                    UC120ReportState(DeviceContext, 0, Power, Type, Type, Polarity);
                    DeviceContext->InternalState[2] = 0;
                    DeviceContext->InternalState[6] = Power;
                    DeviceContext->InternalState[18] = Polarity;
                    DeviceContext->InternalState[10] = Type;
                    UC120ToggleReg4YetUnknown(DeviceContext, 0);
                }
                break;
            default:
                break;
            }
        }
        Register2Bits = DeviceContext->Register2 >> 6;
        if (Register2Bits)
        {
            StateIndex = 4;
            switch (Register2Bits)
            {
            case 1u:
                StateIndex = 0;
                break;
            case 2u:
                StateIndex = 1;
                break;
            case 3u:
                StateIndex = 2;
                break;
            }
            if (StateIndex == DeviceContext->InternalState[14])
            {
                return;
            }
            // result = sub_406D0C(v1, 2, 2, 7);
            UC120ReportState(DeviceContext, 2, 2, 7, StateIndex, 0);
            DeviceContext->InternalState[14] = StateIndex;
        }
    }
    if (DeviceContext->Register2 & 2) {
        UC120ProcessIncomingPdMessage(DeviceContext);
    }
    if (DeviceContext->Register2 & 1) {
        UC120SynchronizeIncomingMessageSize(DeviceContext);
    }
}

BOOLEAN PlugdetInterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(MessageId);

    return TRUE;
}

