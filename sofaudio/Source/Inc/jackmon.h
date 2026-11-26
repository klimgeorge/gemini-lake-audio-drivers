// Headphone jack detection for the SOF audio driver.
//
// The DA7219 codec driver (CoolStar, vendored in ../../da7219) reports jack
// plug/unplug as a vendor HID input report on its second top-level
// collection (HID\VID_2DCF&PID_7219&Col02):
//   { ReportID 0x02, ControlCode 0x01 (jack type), ControlValue }
// ControlValue is 0 when nothing is plugged in, non-zero otherwise. Writing
// the output report { 0x02, 0x00 } makes it send the current state.
//
// jackmon opens that collection, reads the reports on a system thread and
// tells the headphone topology filter, which raises
// KSEVENT_PINCAPS_JACKINFOCHANGE so Windows shows the Headphones endpoint as
// plugged in / unplugged.
#pragma once

// Start/stop watching for the DA7219 HID collection (PASSIVE_LEVEL).
NTSTATUS JackMonStart(_In_ PDRIVER_OBJECT DriverObject);
VOID JackMonStop();

// TRUE while headphones are plugged in. Also TRUE until the codec has
// reported anything, so the endpoint stays usable without the codec driver.
BOOLEAN JackMonIsHeadphoneConnected();

// The headphone topology miniport registers itself so jack changes can raise
// its pin event. Clear must be called before the miniport is freed.
VOID JackMonSetHeadphoneTopology(_In_ PVOID Topology);
VOID JackMonClearHeadphoneTopology(_In_ PVOID Topology);

// Implemented in mintopo.cpp: raises KSEVENT_PINCAPS_JACKINFOCHANGE on the
// headphone bridge pin of the given topology miniport.
VOID SofGenerateHeadphoneJackEvent(_In_ PVOID Topology);
