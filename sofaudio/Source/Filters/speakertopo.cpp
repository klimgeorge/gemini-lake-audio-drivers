/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakertopo.cpp

Abstract:

    Implementation of topology miniport for the speaker (internal).
--*/

#pragma warning (disable : 4127)

#include "definitions.h"
#include "endpoints.h"
#include "mintopo.h"
#include "speakertopo.h"
#include "speakertoptable.h"
#include "headphonetoptable.h"
#include "jackmon.h"


#pragma code_seg("PAGE")
//=============================================================================
NTSTATUS
PropertyHandler_SpeakerTopoFilter
( 
    _In_ PPCPROPERTY_REQUEST      PropertyRequest 
)
/*++

Routine Description:

  Redirects property request to miniport object

Arguments:

  PropertyRequest - 

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    DPF_ENTER(("[PropertyHandler_SpeakerTopoFilter]"));

    // PropertryRequest structure is filled by portcls. 
    // MajorTarget is a pointer to miniport object for miniports.
    //
    NTSTATUS            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    PCMiniportTopology  pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;

    if (IsEqualGUIDAligned(*PropertyRequest->PropertyItem->Set, KSPROPSETID_Jack))
    {
        if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription(
                PropertyRequest,
                ARRAYSIZE(SpeakerJackDescriptions),
                SpeakerJackDescriptions
                );
        }
        else if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION2)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription2(
                PropertyRequest,
                ARRAYSIZE(SpeakerJackDescriptions),
                SpeakerJackDescriptions,
                0 // jack capabilities
                );
        }
    }

    return ntStatus;
} // PropertyHandler_SpeakerTopoFilter

//=============================================================================
NTSTATUS
PropertyHandler_SpeakerTopology
(
    _In_ PPCPROPERTY_REQUEST      PropertyRequest
)
/*++

Routine Description:

  Redirects property request to miniport object

Arguments:

  PropertyRequest -

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    DPF_ENTER(("[PropertyHandler_SpeakerTopology]"));

    // PropertryRequest structure is filled by portcls. 
    // MajorTarget is a pointer to miniport object for miniports.
    //
    PCMiniportTopology pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;

    return pMiniport->PropertyHandlerGeneric(PropertyRequest);
} // PropertyHandler_SpeakerTopology

#pragma code_seg()

//=============================================================================
// SOF: headphone endpoint handlers (same as the speaker ones, but report the
// 3.5 mm jack description from headphonetoptable.h)
//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS
PropertyHandler_HeadphoneTopoFilter
(
    _In_ PPCPROPERTY_REQUEST      PropertyRequest
)
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    NTSTATUS            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    PCMiniportTopology  pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;

    if (IsEqualGUIDAligned(*PropertyRequest->PropertyItem->Set, KSPROPSETID_Jack))
    {
        // Live jack state from the DA7219 codec driver (see jackmon.h).
        HeadphoneJackDescBridge.IsConnected = JackMonIsHeadphoneConnected();

        if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription(
                PropertyRequest,
                ARRAYSIZE(HeadphoneJackDescriptions),
                HeadphoneJackDescriptions
                );
        }
        else if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION2)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription2(
                PropertyRequest,
                ARRAYSIZE(HeadphoneJackDescriptions),
                HeadphoneJackDescriptions,
                JACKDESC2_PRESENCE_DETECT_CAPABILITY
                );
        }
    }

    return ntStatus;
} // PropertyHandler_HeadphoneTopoFilter

#pragma code_seg("PAGE")
NTSTATUS
PropertyHandler_HeadphoneTopology
(
    _In_ PPCPROPERTY_REQUEST      PropertyRequest
)
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    PCMiniportTopology pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;

    return pMiniport->PropertyHandlerGeneric(PropertyRequest);
} // PropertyHandler_HeadphoneTopology

//=============================================================================
// SOF: KSEVENT_PINCAPS_JACKINFOCHANGE on the headphone bridge pin. PortCls
// keeps the event list; jackmon fires it through GenerateEventList.
#pragma code_seg()
NTSTATUS
EventHandler_HeadphoneJack
(
    _In_ PPCEVENT_REQUEST EventRequest
)
{
    ASSERT(EventRequest);

    PCMiniportTopology pMiniport = (PCMiniportTopology)EventRequest->MajorTarget;

    switch (EventRequest->Verb)
    {
    case PCEVENT_VERB_SUPPORT:
    case PCEVENT_VERB_REMOVE:
        return STATUS_SUCCESS;

    case PCEVENT_VERB_ADD:
        if (EventRequest->EventEntry == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        pMiniport->AddEventToEventList(EventRequest->EventEntry);
        return STATUS_SUCCESS;

    default:
        return STATUS_INVALID_PARAMETER;
    }
} // EventHandler_HeadphoneJack

#pragma code_seg()
