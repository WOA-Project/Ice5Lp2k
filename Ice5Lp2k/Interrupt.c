// Interrupt.c: interrupt handler routine

#include <Driver.h>
#include "Interrupt.tmh"

BOOLEAN UC120InterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageID)
{
	WDFDEVICE Device;
	PDEVICE_CONTEXT pDeviceContext;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(MessageID);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);

	WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, NULL);

	status = UC120SpiRead(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
		pDeviceContext->Register2 = 0xff;
	}
	else
	{
		UC120InterruptIsrInternal(pDeviceContext);
	}

	pDeviceContext->Register2 = 0xFFu;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
	}

	WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return TRUE;
}

void UC120InterruptIsrInternal(PDEVICE_CONTEXT DeviceContext)
{
	NTSTATUS status;
	UCHAR Role;
	UCHAR Reg7Value;
	unsigned short Polarity;
	UCHAR CableType;
	UC120_ADVERTISED_CURRENT_LEVEL Current;
	UCHAR CurrentChanged;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	if (DeviceContext->Register2 & 0xFC)
	{
		status = UC120SpiRead(&DeviceContext->SpiDevice, 7, &DeviceContext->Register7, 1u);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
			return;
		}

		Role = (DeviceContext->Register2 & 0x3Cu) >> 2;
		if (Role)
		{
			Reg7Value = DeviceContext->Register7;

			Polarity = (Reg7Value & 0x40) ? 2 : 1; // 1: EnumMuxStateUSBStraight, 2: EnumMuxStateUSBTwisted
			CableType = (Reg7Value >> 4) & 3;

			switch (CableType)
			{
			case 1:
				Current = Uc120AdvertisedCurrentLevelDefaultUsb;
				break;
			case 2:
				Current = Uc120AdvertisedCurrentLevelPower15;
				break;
			case 3:
				Current = Uc120AdvertisedCurrentLevelPower30;
				break;
			default:
				Current = Uc120AdvertisedCurrentLevelNotAvailable;
				break;
			}

			switch (Role)
			{
			case 1u:
			case 4u:
				// Detached
				if (DeviceContext->Uc120Event != Uc120EventDetach)
				{
					UC120ReportState(DeviceContext, Uc120EventDetach, Uc120PortTypeUnknown, Uc120PortPartnerTypeUnknown, Uc120AdvertisedCurrentLevelUnknown, 0);

					DeviceContext->Uc120Event = Uc120EventDetach;
					DeviceContext->Uc120PortType = Uc120PortTypeUnknown;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypeUnknown;
					DeviceContext->AdvertisedCurrentLevel = Uc120AdvertisedCurrentLevelUnknown;
					DeviceContext->Orientation = 0;
					UC120ToggleReg4YetUnknown(DeviceContext, 1);

					SetVConn(DeviceContext, 0);
					SetPowerRole(DeviceContext, 0);
				}
				break;
			case 2u:
				SetVConn(DeviceContext, 1);
				SetPowerRole(DeviceContext, 1);

				// USB Host, VBUS ON
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeDfp, Uc120PortPartnerTypeUfp, Current, Polarity);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeDfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypeUfp;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = Polarity;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			case 3u:
				SetVConn(DeviceContext, 1);
				SetPowerRole(DeviceContext, 1);

				// USB Host, VBUS OFF
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeDfp, Uc120PortPartnerTypePoweredCableUfp, Current, Polarity);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeDfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypePoweredCableUfp;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = Polarity;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			case 5u:
				// USB FN, VBUS OFF
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeUfp, Uc120PortPartnerTypeDfp, Current, Polarity);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeUfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypeDfp;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = Polarity;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			case 6u:
				// USB Host, VBUS ON
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeDfp, Uc120PortPartnerTypeAudioAccessory, Current, 0);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeDfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypeAudioAccessory;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = 0;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			case 7u:
				// USB Host, VBUS ON
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeDfp, Uc120PortPartnerTypeDebugAccessory, Current, 0);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeDfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypeDebugAccessory;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = 0;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			case 8u:
				SetVConn(DeviceContext, 1);
				SetPowerRole(DeviceContext, 1);

				// USB Host, VBUS OFF
				if (DeviceContext->Uc120Event != Uc120EventAttach)
				{
					UC120ReportState(DeviceContext, Uc120EventAttach, Uc120PortTypeUfp, Uc120PortPartnerTypePoweredAccessory, Current, Polarity);

					DeviceContext->Uc120Event = Uc120EventAttach;
					DeviceContext->Uc120PortType = Uc120PortTypeUfp;
					DeviceContext->PortPartnerType = Uc120PortPartnerTypePoweredAccessory;
					DeviceContext->AdvertisedCurrentLevel = Current;
					DeviceContext->Orientation = Polarity;

					UC120ToggleReg4YetUnknown(DeviceContext, 0);
				}
				break;
			default:
				break;
			}
		}

		CurrentChanged = DeviceContext->Register2 >> 6;
		if (CurrentChanged)
		{
			switch (CurrentChanged)
			{
			case 1:
				Current = Uc120AdvertisedCurrentLevelDefaultUsb;
				break;
			case 2:
				Current = Uc120AdvertisedCurrentLevelPower15;
				break;
			case 3:
				Current = Uc120AdvertisedCurrentLevelPower30;
				break;
			default:
				Current = Uc120AdvertisedCurrentLevelUnknown;
				break;
			}

			if (Current == DeviceContext->AdvertisedCurrentLevel)
			{
				TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
				return;
			}

			UC120ReportState(DeviceContext, Uc120EventCurrentLevelChange, Uc120PortTypeUnknown, Uc120PortPartnerTypeUnknown, Current, 0);
			pDeviceContext->AdvertisedCurrentLevel = UsbCurrentType;
		}
	}

	if (DeviceContext->Register2 & 2)
	{
		UC120ProcessIncomingPdMessage(DeviceContext);
	}
	if (DeviceContext->Register2 & 1)
	{
		UC120SynchronizeIncomingMessageSize(DeviceContext);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

BOOLEAN PlugdetInterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
	UNREFERENCED_PARAMETER(Interrupt);
	UNREFERENCED_PARAMETER(MessageId);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return TRUE;
}

BOOLEAN PmicInterrupt1Isr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
	WDFDEVICE Device;
	NTSTATUS status;
	PDEVICE_CONTEXT pDeviceContext;

	UNREFERENCED_PARAMETER(MessageId);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);

	WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, 0);
	pDeviceContext->Register5 |= 0x40u;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 5, &pDeviceContext->Register5, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
	}

	WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
	WdfInterruptQueueWorkItemForIsr(Interrupt);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return TRUE;
}

BOOLEAN PmicInterrupt2Isr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
	WDFDEVICE Device;
	NTSTATUS status;
	UCHAR RegisterValue;
	PDEVICE_CONTEXT pDeviceContext;

	UNREFERENCED_PARAMETER(MessageId);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);

	WdfWaitLockAcquire(pDeviceContext->DeviceWaitLock, 0);
	RegisterValue = 0;

	if (UC120SpiRead(&pDeviceContext->SpiDevice, 5, &RegisterValue, 1u) < 0 || RegisterValue & 0x40)
	{
		pDeviceContext->Register5 &= 0xBFu;
		status = UC120SpiWrite(&pDeviceContext->SpiDevice, 5, &pDeviceContext->Register5, 1);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
		}

		WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);

		// Cable detach
		UC120ReportState(pDeviceContext, Uc120EventDetach, Uc120PortTypeUnknown, Uc120PortPartnerTypeUnknown, Uc120AdvertisedCurrentLevelUnknown, 0);

		pDeviceContext->Uc120Event = Uc120EventDetach;
		pDeviceContext->Uc120PortType = Uc120PortTypeUnknown;
		pDeviceContext->PortPartnerType = Uc120PortPartnerTypeUnknown;
		pDeviceContext->AdvertisedCurrentLevel = Uc120AdvertisedCurrentLevelUnknown;
		pDeviceContext->Orientation = 0;
		UC120ToggleReg4YetUnknown(pDeviceContext, 1);
	}
	else
	{
		WdfWaitLockRelease(pDeviceContext->DeviceWaitLock);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return TRUE;
}

void PmicInterrupt1WorkItem(WDFINTERRUPT Interrupt, WDFOBJECT AssociatedObject)
{
	WDFDEVICE Device;
	PDEVICE_CONTEXT pDeviceContext;
	LARGE_INTEGER Delay;

	UNREFERENCED_PARAMETER(AssociatedObject);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);

	Delay.QuadPart = 0xFFFFFFFFFFB3B4C0;
	KeDelayExecutionThread(0, 0, &Delay);

	if (pDeviceContext->Uc120Event == Uc120EventDetach && pDeviceContext->Register5 & 0x40)
	{
		UC120ReportState(pDeviceContext, Uc120EventAttach, Uc120PortTypeDfp, Uc120PortPartnerTypeUfp, Uc120AdvertisedCurrentLevelDefaultUsb, 0);

		// Possible workaround for a bug? Swaps partner/host states
		pDeviceContext->Orientation = 0;
		pDeviceContext->PortPartnerType = Uc120PortPartnerTypeDfp; //?? It is like this in the original driver
		pDeviceContext->AdvertisedCurrentLevel = Uc120AdvertisedCurrentLevelDefaultUsb;
		pDeviceContext->Uc120PortType = Uc120PortTypeUfp; //?? It is like this in the original driver
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

NTSTATUS UC120InterruptEnable(WDFINTERRUPT Interrupt, WDFDEVICE AssociatedDevice)
{
	WDFDEVICE Device;
	PDEVICE_CONTEXT pDeviceContext;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(AssociatedDevice);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);
	pDeviceContext->Register2 = 0xFFu;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
		goto exit;
	}

	pDeviceContext->Register3 = 0xFFu;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 3, &pDeviceContext->Register3, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
		goto exit;
	}

	pDeviceContext->Register4 |= 1u;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 4, &pDeviceContext->Register4, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
		goto exit;
	}

	pDeviceContext->Register5 &= 0x7Fu;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 5, &pDeviceContext->Register5, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
		goto exit;
	}

	UC120SpiRead(&pDeviceContext->SpiDevice, 2, &pDeviceContext->Register2, 1u);
	status = UC120SpiRead(&pDeviceContext->SpiDevice, 7, &pDeviceContext->Register7, 1u);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
		goto exit;
	}

exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return status;
}

NTSTATUS UC120InterruptDisable(WDFINTERRUPT Interrupt, WDFDEVICE AssociatedDevice)
{
	WDFDEVICE Device;
	PDEVICE_CONTEXT pDeviceContext;
	NTSTATUS status;

	UNREFERENCED_PARAMETER(AssociatedDevice);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	Device = WdfInterruptGetDevice(Interrupt);
	pDeviceContext = DeviceGetContext(Device);

	pDeviceContext->Register4 &= 0xFEu;
	status = UC120SpiWrite(&pDeviceContext->SpiDevice, 4, &pDeviceContext->Register4, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return status;
}
