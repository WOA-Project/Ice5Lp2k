/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_Ice5Lp2k,
    0xadf52d32,0x1ff9,0x4a55,0xbf,0x7a,0x49,0xfc,0xd3,0xdf,0x11,0x8e);
// {adf52d32-1ff9-4a55-bf7a-49fcd3df118e}
