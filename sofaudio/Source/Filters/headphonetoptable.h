/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakertoptable.h

Abstract:

    Declaration of topology tables.
--*/

#ifndef _SIMPLEAUDIOSAMPLE_HEADPHONETOPTABLE_H_
#define _SIMPLEAUDIOSAMPLE_HEADPHONETOPTABLE_H_

//=============================================================================
static
KSDATARANGE HeadphoneTopoPinDataRangesBridge[] =
{
 {
   sizeof(KSDATARANGE),
   0,
   0,
   0,
   STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
   STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
   STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
 }
};

//=============================================================================
static
PKSDATARANGE HeadphoneTopoPinDataRangePointersBridge[] =
{
  &HeadphoneTopoPinDataRangesBridge[0]
};

//=============================================================================
// SOF: jack presence change notifications for the headphone bridge pin.
// These must be in the FILTER automation table: Windows enables the event on
// the filter handle with KSE_PIN (pin id), because a bridge pin can't be
// opened, so a pin-level automation table is never used.
static
PCEVENT_ITEM HeadphoneJackEvents[] =
{
    {
        &KSEVENTSETID_PinCapsChange,
        KSEVENT_PINCAPS_JACKINFOCHANGE,
        KSEVENT_TYPE_ENABLE | KSEVENT_TYPE_BASICSUPPORT,
        EventHandler_HeadphoneJack
    }
};

//=============================================================================
static
PCPIN_DESCRIPTOR HeadphoneTopoMiniportPins[] =
{
  // KSPIN_TOPO_WAVEOUT_SOURCE
  {
    0,
    0,
    0,                                                  // InstanceCount
    NULL,                                               // AutomationTable
    {                                                   // KsPinDescriptor
      0,                                                // InterfacesCount
      NULL,                                             // Interfaces
      0,                                                // MediumsCount
      NULL,                                             // Mediums
      SIZEOF_ARRAY(HeadphoneTopoPinDataRangePointersBridge),// DataRangesCount
      HeadphoneTopoPinDataRangePointersBridge,            // DataRanges
      KSPIN_DATAFLOW_IN,                                // DataFlow
      KSPIN_COMMUNICATION_NONE,                         // Communication
      &KSCATEGORY_AUDIO,                                // Category
      NULL,                                             // Name
      0                                                 // Reserved
    }
  },
  // KSPIN_TOPO_LINEOUT_DEST
  {
    0,
    0,
    0,                                                  // InstanceCount
    NULL,                                               // AutomationTable (jack events are on the filter)
    {                                                   // KsPinDescriptor
      0,                                                // InterfacesCount
      NULL,                                             // Interfaces
      0,                                                // MediumsCount
      NULL,                                             // Mediums
      SIZEOF_ARRAY(HeadphoneTopoPinDataRangePointersBridge),// DataRangesCount
      HeadphoneTopoPinDataRangePointersBridge,            // DataRanges
      KSPIN_DATAFLOW_OUT,                               // DataFlow
      KSPIN_COMMUNICATION_NONE,                         // Communication
      &KSNODETYPE_HEADPHONES,                              // Category
      NULL,                                             // Name
      0                                                 // Reserved
    }
  }
};

//=============================================================================
static
KSJACK_DESCRIPTION HeadphoneJackDescBridge =
{
    KSAUDIO_SPEAKER_STEREO,
    JACKDESC_RGB(0x00,0x00,0x00),              // black 3.5 mm combo jack
    eConnType3Point5mm,
    eGeoLocLeft,
    eGenLocPrimaryBox,
    ePortConnJack,
    TRUE                                        // updated from jackmon when the property is read
};

// Only return a KSJACK_DESCRIPTION for the physical bridge pin.
static 
PKSJACK_DESCRIPTION HeadphoneJackDescriptions[] =
{
    NULL,
    &HeadphoneJackDescBridge
};

//=============================================================================
static
PCPROPERTY_ITEM HeadphonePropertiesVolume[] =
{
    {
    &KSPROPSETID_Audio,
    KSPROPERTY_AUDIO_VOLUMELEVEL,
    KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
    PropertyHandler_HeadphoneTopology
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationHeadphoneVolume, HeadphonePropertiesVolume);

//=============================================================================
static
PCPROPERTY_ITEM HeadphonePropertiesMute[] =
{
  {
    &KSPROPSETID_Audio,
    KSPROPERTY_AUDIO_MUTE,
    KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
    PropertyHandler_HeadphoneTopology
  }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationHeadphoneMute, HeadphonePropertiesMute);

//=============================================================================
static
PCNODE_DESCRIPTOR HeadphoneTopologyNodes[] =
{
    // KSNODE_TOPO_VOLUME
    {
      0,                              // Flags
      &AutomationHeadphoneVolume,     // AutomationTable
      &KSNODETYPE_VOLUME,             // Type
      &KSAUDFNAME_MASTER_VOLUME         // Name
    },
    // KSNODE_TOPO_MUTE
    {
      0,                              // Flags
      &AutomationHeadphoneMute,       // AutomationTable
      &KSNODETYPE_MUTE,               // Type
      &KSAUDFNAME_MASTER_MUTE            // Name
    }
};

C_ASSERT(KSNODE_TOPO_VOLUME == 0);
C_ASSERT(KSNODE_TOPO_MUTE == 1);

static
PCCONNECTION_DESCRIPTOR HeadphoneTopoMiniportConnections[] =
{
    //  FromNode,                 FromPin,                    ToNode,                 ToPin
    {   PCFILTER_NODE,            KSPIN_TOPO_WAVEOUT_SOURCE,    KSNODE_TOPO_VOLUME,     1 },
    {   KSNODE_TOPO_VOLUME,       0,                          KSNODE_TOPO_MUTE,       1 },
    {   KSNODE_TOPO_MUTE,         0,                          PCFILTER_NODE,          KSPIN_TOPO_LINEOUT_DEST }
};

//=============================================================================
static
PCPROPERTY_ITEM PropertiesHeadphoneTopoFilter[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET |
        KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_HeadphoneTopoFilter
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION2,
        KSPROPERTY_TYPE_GET |
        KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_HeadphoneTopoFilter
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(AutomationHeadphoneTopoFilter, PropertiesHeadphoneTopoFilter, HeadphoneJackEvents);

//=============================================================================
static
PCFILTER_DESCRIPTOR HeadphoneTopoMiniportFilterDescriptor =
{
  0,                                            // Version
  &AutomationHeadphoneTopoFilter,                 // AutomationTable
  sizeof(PCPIN_DESCRIPTOR),                     // PinSize
  SIZEOF_ARRAY(HeadphoneTopoMiniportPins),        // PinCount
  HeadphoneTopoMiniportPins,                      // Pins
  sizeof(PCNODE_DESCRIPTOR),                    // NodeSize
  SIZEOF_ARRAY(HeadphoneTopologyNodes),           // NodeCount
  HeadphoneTopologyNodes,                         // Nodes
  SIZEOF_ARRAY(HeadphoneTopoMiniportConnections), // ConnectionCount
  HeadphoneTopoMiniportConnections,               // Connections
  0,                                            // CategoryCount
  NULL                                          // Categories
};

#endif // _SIMPLEAUDIOSAMPLE_HEADPHONETOPTABLE_H_
