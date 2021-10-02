// Device.h: device context and function definitions

#include <public.h>

EXTERN_C_START

typedef union _UC120_REGISTER_2 {
	BYTE Buffer;
	struct {
		BYTE PDMessageSizeChanged : 1;
		BYTE HasIncomingPDMessage : 1;
		BYTE State : 4;
		BYTE AdvertisedCurrentLevel : 2;
	} Data;
} UC120_REGISTER_2, * PUC120_REGISTER_2;

typedef union _UC120_REGISTER_7 {
	BYTE Buffer;
	struct {
		BYTE Reserved0 : 4;
		BYTE AdvertisedCurrentLevel : 2;
		BYTE Polarity : 1;
		BYTE Reserved1 : 1;
	} Data;
} UC120_REGISTER_7, * PUC120_REGISTER_7;

typedef struct _SPI_DEVICE_CONNECTION {
	ULONG SpiDeviceIdLow;
	ULONG SpiDeviceIdHigh;
	WDFIOTARGET SpiDeviceIoTarget;
} SPI_DEVICE_CONNECTION, * PSPI_DEVICE_CONNECTION;

typedef struct _DEVICE_CONTEXT
{
	WDFDEVICE Device;
	SPI_DEVICE_CONNECTION SpiDevice;

	ULONG GpioDeviceIdLow;
	ULONG GpioDeviceIdHigh;

	WDFINTERRUPT Uc120Interrupt;
	WDFINTERRUPT PlugDetInterrupt;
	WDFINTERRUPT Pmic1Interrupt;
	WDFINTERRUPT Pmic2Interupt;

	WDFQUEUE DefaultIoQueue;
	WDFQUEUE DelayedIoCtlQueue;
	WDFQUEUE PdReadQueue;
	WDFQUEUE PdWriteQueue;

	WDFCOLLECTION DelayedQueryIoctlRequestCol;

	UCHAR Register0;
	UCHAR Register1;
	UC120_REGISTER_2 Register2;
	UCHAR Register3;
	UCHAR Register4;
	UCHAR Register5;
	UCHAR Register6;
	UC120_REGISTER_7 Register7;
	UCHAR Register8;
	UCHAR Register13;

	UC120_EVENT Uc120Event;
	UC120_PORT_TYPE Uc120PortType;
	UC120_ADVERTISED_CURRENT_LEVEL AdvertisedCurrentLevel;
	UC120_PORT_PARTNER_TYPE PortPartnerType;
	unsigned short Orientation;

	UCHAR PDMessageType;

	KEVENT PdEvent;

	WDFWAITLOCK DeviceWaitLock;

	// When UC120 is calibrated, set to 1
	UCHAR Calibrated;
} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Function to initialize the device and its callbacks
//
NTSTATUS
Ice5Lp2kCreateDevice(
	_Inout_ PWDFDEVICE_INIT DeviceInit
);

EVT_WDF_DEVICE_PREPARE_HARDWARE UC120EvtDevicePrepareHardware;
EVT_WDF_DEVICE_D0_ENTRY UC120EvtDeviceD0Entry;
EVT_WDF_IO_QUEUE_IO_WRITE Ice5Lp2kQueueEvtWrite;

EVT_WDF_INTERRUPT_ISR UC120InterruptIsr;
EVT_WDF_INTERRUPT_ENABLE UC120InterruptEnable;
EVT_WDF_INTERRUPT_DISABLE UC120InterruptDisable;

EVT_WDF_INTERRUPT_ISR PlugdetInterruptIsr;

EVT_WDF_INTERRUPT_ISR PmicInterrupt1Isr;
EVT_WDF_INTERRUPT_WORKITEM PmicInterrupt1WorkItem;

EVT_WDF_INTERRUPT_ISR PmicInterrupt2Isr;

void UC120InterruptIsrInternal(PDEVICE_CONTEXT DeviceContext);

NTSTATUS UC120SpiRead(
	_In_ PSPI_DEVICE_CONNECTION Connection,
	_In_ UCHAR RegisterAddr, _Out_ PUCHAR RegisterValue, _In_ size_t RegReadSize
);

NTSTATUS UC120SpiWrite(
	_In_ PSPI_DEVICE_CONNECTION Connection,
	_In_ UCHAR RegisterAddr, _In_ PVOID Content, _In_ size_t Size
);

NTSTATUS UC120ReportState(
	PDEVICE_CONTEXT DeviceContext,
	UC120_EVENT MessageType,
	UC120_PORT_TYPE Power,
	UC120_PORT_PARTNER_TYPE PartnerType,
	UC120_ADVERTISED_CURRENT_LEVEL UsbCurrentType,
	unsigned short Polarity);

NTSTATUS UC120ToggleReg4YetUnknown(PDEVICE_CONTEXT DeviceContext, UCHAR Bit);
void UC120ProcessIncomingPdMessage(PDEVICE_CONTEXT DeviceContext);
void UC120SynchronizeIncomingMessageSize(PDEVICE_CONTEXT DeviceContext);
void UC120FulfillIncomingMessage(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);

NTSTATUS SetVConn(PDEVICE_CONTEXT DeviceContext, UCHAR Enable);
NTSTATUS SetPowerRole(PDEVICE_CONTEXT DeviceContext, UCHAR PowerRole);

NTSTATUS UC120AcquireInitializeResourcesFromAcpi(PDEVICE_CONTEXT DeviceContext, WDFCMRESLIST ResourcesTranslated);
NTSTATUS UC120OpenResources(WDFDEVICE Device, PSPI_DEVICE_CONNECTION ConnectionInfo);
NTSTATUS UC120Calibrate(PDEVICE_CONTEXT DeviceContext);

void UC120PDMessagingEnable(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);
void UC120IoctlExecuteHardReset(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);
void UC120GetCableDetectionState(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);
void UC120SetPortDataRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);
void UC120IoctlReportNewPowerRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);
void UC120IoctlSetVConnRoleSwitch(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request);

#define UC120_CALIBRATIONFILE_SIZE 11

EXTERN_C_END
