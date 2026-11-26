// Headphone jack detection: reads the DA7219 codec driver's vendor HID
// reports. See Inc/jackmon.h.

#include "definitions.h"
#include "jackmon.h"

#define JACKMON_POOLTAG 'kcaJ'

// {4D1E55B2-F16F-11CF-88CB-001111000030} GUID_DEVINTERFACE_HID
static const GUID JackMonHidInterface =
    { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
// {CB3A4004-46F0-11D0-B08F-00609713053F} GUID_DEVICE_INTERFACE_ARRIVAL
static const GUID JackMonInterfaceArrival =
    { 0xcb3a4004, 0x46f0, 0x11d0, { 0xb0, 0x8f, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3f } };

#define DA7219_REPORTID_SPECKEYS      0x02
#define DA7219_CONTROL_CODE_JACK_TYPE 0x01
#define DA7219_INPUT_REPORT_LEN       3   // ID + control code + value
#define DA7219_OUTPUT_REPORT_LEN      2   // ID + request byte

typedef struct _JACKMON_STATE {
    BOOLEAN         Initialized;
    BOOLEAN         Started;
    FAST_MUTEX      Lock;           // Thread, Running, Link
    PVOID           Notification;   // IoRegisterPlugPlayNotification handle
    PETHREAD        Thread;         // referenced reader thread
    BOOLEAN         Running;        // reader thread is active
    KEVENT          StopEvent;
    WCHAR           LinkBuf[512];
    UNICODE_STRING  Link;

    // Reader I/O buffers. Kept here, not on the thread stack, so a late
    // completion after a cancel can never write to a dead stack frame.
    IO_STATUS_BLOCK ReadIosb;
    UCHAR           ReadBuf[DA7219_INPUT_REPORT_LEN];
    IO_STATUS_BLOCK WriteIosb;
    UCHAR           WriteBuf[DA7219_OUTPUT_REPORT_LEN];

    FAST_MUTEX      TopoLock;       // HeadphoneTopo
    PVOID           HeadphoneTopo;
} JACKMON_STATE;

static JACKMON_STATE g_Jack;
static volatile LONG g_HeadphoneConnected = 1;   // "connected" until told otherwise

static VOID JackMonEnsureInit()
{
    if (!g_Jack.Initialized) {
        ExInitializeFastMutex(&g_Jack.Lock);
        ExInitializeFastMutex(&g_Jack.TopoLock);
        KeInitializeEvent(&g_Jack.StopEvent, NotificationEvent, FALSE);
        g_Jack.Initialized = TRUE;
    }
}

BOOLEAN JackMonIsHeadphoneConnected()
{
    return g_HeadphoneConnected != 0;
}

VOID JackMonSetHeadphoneTopology(_In_ PVOID Topology)
{
    JackMonEnsureInit();
    ExAcquireFastMutex(&g_Jack.TopoLock);
    g_Jack.HeadphoneTopo = Topology;
    ExReleaseFastMutex(&g_Jack.TopoLock);
}

VOID JackMonClearHeadphoneTopology(_In_ PVOID Topology)
{
    JackMonEnsureInit();
    ExAcquireFastMutex(&g_Jack.TopoLock);
    if (g_Jack.HeadphoneTopo == Topology) {
        g_Jack.HeadphoneTopo = NULL;
    }
    ExReleaseFastMutex(&g_Jack.TopoLock);
}

static VOID JackMonSetState(_In_ BOOLEAN Connected)
{
    LONG newValue = Connected ? 1 : 0;
    LONG old = InterlockedExchange(&g_HeadphoneConnected, newValue);
    DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL,
        "sofaudio: headphone jack %s\n", Connected ? "plugged in" : "unplugged");
    if (old == newValue) {
        return;
    }
    // Raise the event while holding TopoLock: the miniport destructor takes
    // the same lock, so the miniport can't be freed during the call.
    ExAcquireFastMutex(&g_Jack.TopoLock);
    if (g_Jack.HeadphoneTopo) {
        SofGenerateHeadphoneJackEvent(g_Jack.HeadphoneTopo);
    }
    ExReleaseFastMutex(&g_Jack.TopoLock);
}

// Case-insensitive "does Haystack contain Needle" for symbolic link names.
static BOOLEAN JackMonContains(_In_ PCUNICODE_STRING Haystack, _In_ PCWSTR Needle)
{
    SIZE_T n = wcslen(Needle);
    SIZE_T h = Haystack->Length / sizeof(WCHAR);
    if (n == 0 || n > h) {
        return FALSE;
    }
    for (SIZE_T i = 0; i + n <= h; i++) {
        SIZE_T j = 0;
        for (; j < n; j++) {
            if (RtlUpcaseUnicodeChar(Haystack->Buffer[i + j]) != RtlUpcaseUnicodeChar(Needle[j])) {
                break;
            }
        }
        if (j == n) {
            return TRUE;
        }
    }
    return FALSE;
}

// Waits for an async ZwReadFile/ZwWriteFile, or for the stop event.
// Returns STATUS_SUCCESS when the I/O finished, STATUS_CANCELLED on stop,
// STATUS_TIMEOUT on timeout.
static NTSTATUS JackMonWaitIo(_In_ PKEVENT IoEvent, _In_opt_ PLARGE_INTEGER Timeout)
{
    PVOID objects[2] = { &g_Jack.StopEvent, IoEvent };
    NTSTATUS st = KeWaitForMultipleObjects(2, objects, WaitAny, Executive, KernelMode,
        FALSE, Timeout, NULL);
    if (st == STATUS_WAIT_0) {
        return STATUS_CANCELLED;
    }
    if (st == STATUS_WAIT_1) {
        return STATUS_SUCCESS;
    }
    return STATUS_TIMEOUT;
}

static VOID JackMonThread(_In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    HANDLE file = NULL;
    HANDLE evtHandle = NULL;
    PKEVENT evt = NULL;
    NTSTATUS st;

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &g_Jack.Link, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK iosb;
    st = ZwCreateFile(&file, GENERIC_READ | GENERIC_WRITE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    if (!NT_SUCCESS(st)) {
        DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL,
            "sofaudio: open DA7219 HID collection failed 0x%08X\n", st);
        file = NULL;
        goto out;
    }

    OBJECT_ATTRIBUTES eoa;
    InitializeObjectAttributes(&eoa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    st = ZwCreateEvent(&evtHandle, EVENT_ALL_ACCESS, &eoa, NotificationEvent, FALSE);
    if (!NT_SUCCESS(st)) {
        evtHandle = NULL;
        goto out;
    }
    st = ObReferenceObjectByHandle(evtHandle, EVENT_ALL_ACCESS, *ExEventObjectType,
        KernelMode, (PVOID*)&evt, NULL);
    if (!NT_SUCCESS(st)) {
        evt = NULL;
        goto out;
    }

    // Ask the codec driver for the current jack state (it answers with an
    // input report, read below).
    {
        g_Jack.WriteBuf[0] = DA7219_REPORTID_SPECKEYS;
        g_Jack.WriteBuf[1] = 0;
        KeClearEvent(evt);
        st = ZwWriteFile(file, evtHandle, NULL, NULL, &g_Jack.WriteIosb,
            g_Jack.WriteBuf, DA7219_OUTPUT_REPORT_LEN, NULL, NULL);
        if (st == STATUS_PENDING) {
            LARGE_INTEGER timeout;
            timeout.QuadPart = -10LL * 1000 * 1000;   // 1 s
            st = JackMonWaitIo(evt, &timeout);
            if (st == STATUS_CANCELLED) {
                goto out;
            }
        }
    }

    for (;;) {
        KeClearEvent(evt);
        RtlZeroMemory(g_Jack.ReadBuf, sizeof(g_Jack.ReadBuf));
        st = ZwReadFile(file, evtHandle, NULL, NULL, &g_Jack.ReadIosb,
            g_Jack.ReadBuf, DA7219_INPUT_REPORT_LEN, NULL, NULL);
        if (st == STATUS_PENDING) {
            st = JackMonWaitIo(evt, NULL);
            if (st == STATUS_CANCELLED) {
                break;      // stopping; the close below cancels the read
            }
            st = g_Jack.ReadIosb.Status;
        }
        if (!NT_SUCCESS(st)) {
            DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL,
                "sofaudio: DA7219 HID read ended 0x%08X\n", st);
            break;          // device removed or driver stopping
        }
        if (g_Jack.ReadBuf[0] == DA7219_REPORTID_SPECKEYS &&
            g_Jack.ReadBuf[1] == DA7219_CONTROL_CODE_JACK_TYPE) {
            JackMonSetState(g_Jack.ReadBuf[2] != 0);
        }
    }

out:
    if (file) {
        // Closing the handle makes HID class complete any pending read.
        ZwClose(file);
        if (evt) {
            LARGE_INTEGER timeout;
            timeout.QuadPart = -10LL * 1000 * 1000;   // 1 s
            KeWaitForSingleObject(evt, Executive, KernelMode, FALSE, &timeout);
        }
    }
    if (evt) {
        ObDereferenceObject(evt);
    }
    if (evtHandle) {
        ZwClose(evtHandle);
    }

    ExAcquireFastMutex(&g_Jack.Lock);
    g_Jack.Running = FALSE;
    ExReleaseFastMutex(&g_Jack.Lock);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

// Waits for a finished/old reader thread and drops our reference.
// Called with g_Jack.Lock NOT held.
static VOID JackMonReapThread(_In_opt_ PETHREAD Thread)
{
    if (Thread) {
        KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Thread);
    }
}

static NTSTATUS JackMonStartReader(_In_ PCUNICODE_STRING Link)
{
    PETHREAD old = NULL;
    NTSTATUS st = STATUS_SUCCESS;

    ExAcquireFastMutex(&g_Jack.Lock);
    if (!g_Jack.Started || g_Jack.Running) {
        ExReleaseFastMutex(&g_Jack.Lock);
        return STATUS_SUCCESS;      // stopping, or already reading
    }
    old = g_Jack.Thread;
    g_Jack.Thread = NULL;

    if (Link->Length >= sizeof(g_Jack.LinkBuf)) {
        ExReleaseFastMutex(&g_Jack.Lock);
        JackMonReapThread(old);
        return STATUS_NAME_TOO_LONG;
    }
    RtlCopyMemory(g_Jack.LinkBuf, Link->Buffer, Link->Length);
    g_Jack.LinkBuf[Link->Length / sizeof(WCHAR)] = L'\0';
    RtlInitUnicodeString(&g_Jack.Link, g_Jack.LinkBuf);

    HANDLE th = NULL;
    st = PsCreateSystemThread(&th, THREAD_ALL_ACCESS, NULL, NULL, NULL, JackMonThread, NULL);
    if (NT_SUCCESS(st)) {
        g_Jack.Running = TRUE;
        st = ObReferenceObjectByHandle(th, THREAD_ALL_ACCESS, *PsThreadType, KernelMode,
            (PVOID*)&g_Jack.Thread, NULL);
        if (!NT_SUCCESS(st)) {
            g_Jack.Thread = NULL;
            st = STATUS_SUCCESS;    // thread runs anyway; Stop waits on Running
        }
        ZwClose(th);
    }
    ExReleaseFastMutex(&g_Jack.Lock);

    JackMonReapThread(old);
    return st;
}

static NTSTATUS JackMonPnpCallback(_In_ PVOID NotificationStructure, _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    PDEVICE_INTERFACE_CHANGE_NOTIFICATION n = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    if (!IsEqualGUID(n->Event, JackMonInterfaceArrival) || !n->SymbolicLinkName) {
        return STATUS_SUCCESS;
    }
    // DA7219 vendor collection: HID\VID_2DCF&PID_7219&Col02
    if (!JackMonContains(n->SymbolicLinkName, L"VID_2DCF&PID_7219") ||
        !JackMonContains(n->SymbolicLinkName, L"COL02")) {
        return STATUS_SUCCESS;
    }
    NTSTATUS st = JackMonStartReader(n->SymbolicLinkName);
    DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL,
        "sofaudio: DA7219 jack reports at %wZ: 0x%08X\n", n->SymbolicLinkName, st);
    return STATUS_SUCCESS;
}

NTSTATUS JackMonStart(_In_ PDRIVER_OBJECT DriverObject)
{
    JackMonEnsureInit();

    ExAcquireFastMutex(&g_Jack.Lock);
    if (g_Jack.Started) {
        ExReleaseFastMutex(&g_Jack.Lock);
        return STATUS_SUCCESS;
    }
    g_Jack.Started = TRUE;
    KeClearEvent(&g_Jack.StopEvent);
    ExReleaseFastMutex(&g_Jack.Lock);

    // Existing interfaces are reported right away (the codec driver
    // normally starts before us), later arrivals when they happen.
    NTSTATUS st = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (PVOID)&JackMonHidInterface, DriverObject, JackMonPnpCallback, NULL,
        &g_Jack.Notification);
    if (!NT_SUCCESS(st)) {
        g_Jack.Notification = NULL;
        DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL,
            "sofaudio: HID interface notification failed 0x%08X\n", st);
    }
    return st;
}

VOID JackMonStop()
{
    if (!g_Jack.Initialized) {
        return;
    }

    ExAcquireFastMutex(&g_Jack.Lock);
    BOOLEAN wasStarted = g_Jack.Started;
    g_Jack.Started = FALSE;     // no new readers from here on
    ExReleaseFastMutex(&g_Jack.Lock);
    if (!wasStarted) {
        return;
    }

    if (g_Jack.Notification) {
        // Waits for callbacks that are already running.
        IoUnregisterPlugPlayNotificationEx(g_Jack.Notification);
        g_Jack.Notification = NULL;
    }

    KeSetEvent(&g_Jack.StopEvent, IO_NO_INCREMENT, FALSE);

    ExAcquireFastMutex(&g_Jack.Lock);
    PETHREAD t = g_Jack.Thread;
    g_Jack.Thread = NULL;
    ExReleaseFastMutex(&g_Jack.Lock);
    JackMonReapThread(t);

    // Without a reference we can only wait for the Running flag.
    for (int i = 0; i < 300; i++) {
        ExAcquireFastMutex(&g_Jack.Lock);
        BOOLEAN running = g_Jack.Running;
        ExReleaseFastMutex(&g_Jack.Lock);
        if (!running) {
            break;
        }
        LARGE_INTEGER d;
        d.QuadPart = -10LL * 1000 * 10;     // 10 ms
        KeDelayExecutionThread(KernelMode, FALSE, &d);
    }

    // Back to the default ("connected") until the next start reports the real state.
    InterlockedExchange(&g_HeadphoneConnected, 1);
}
