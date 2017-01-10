#include "hotkey.h"
#include "locale.h"
#include "parse.h"
#include <string.h>
#include <pthread.h>
#include <mach/mach_time.h>

#define internal static
#define local_persist static
#define CLOCK_PRECISION 1E-9

extern modifier_state ModifierState;
extern mode DefaultBindingMode;
extern mode *ActiveBindingMode;
extern uint32_t Compatibility;
extern char *FocusedApp;
extern pthread_mutex_t Lock;

internal inline void
ClockGetTime(long long *Time)
{
    local_persist mach_timebase_info_data_t Timebase;
    if(Timebase.denom == 0)
    {
        mach_timebase_info(&Timebase);
    }

    uint64_t Temp = mach_absolute_time();
    *Time = (Temp * Timebase.numer) / Timebase.denom;
}

internal inline double
GetTimeDiff(long long A, long long B)
{
    return (A - B) * CLOCK_PRECISION;
}

internal inline void
UpdatePrefixTimer()
{
    ClockGetTime(&ActiveBindingMode->Time);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, ActiveBindingMode->Timeout * NSEC_PER_SEC),
                   dispatch_get_main_queue(),
    ^{
        if(ActiveBindingMode->Prefix)
        {
            long long Time;
            ClockGetTime(&Time);

            if(GetTimeDiff(Time, ActiveBindingMode->Time) >= ActiveBindingMode->Timeout)
            {
                if(ActiveBindingMode->Restore)
                    ActivateMode(ActiveBindingMode->Restore);
                else
                    ActivateMode("default");
            }
        }
    });
}

internal void
Execute(char *Command)
{
    local_persist char Arg[] = "-c";
    local_persist char *Shell = getenv("SHELL");
    if(!Shell)
    {
        Shell = strdup("/bin/bash");
    }

    int ChildPID = fork();
    if(ChildPID == 0)
    {
        char *Exec[] = { Shell, Arg, Command, NULL};
        int StatusCode = execvp(Exec[0], Exec);
        exit(StatusCode);
    }
}

internal inline void
ExecuteKwmBorderCommand()
{
    char KwmCommand[64] = "kwmc config border focused color ";
    strcat(KwmCommand, ActiveBindingMode->Color);
    Execute(KwmCommand);
}

internal inline bool
VerifyHotkeyType(hotkey *Hotkey)
{
    if(!Hotkey->App)
        return true;

    if(Hotkey->Type == Hotkey_Include)
    {
        pthread_mutex_lock(&Lock);
        char **App = Hotkey->App;
        while(*App)
        {
            if(FocusedApp && StringsAreEqual(*App, FocusedApp))
            {
                pthread_mutex_unlock(&Lock);
                return true;
            }

            ++App;
        }

        pthread_mutex_unlock(&Lock);
        return false;
    }
    else if(Hotkey->Type == Hotkey_Exclude)
    {
        pthread_mutex_lock(&Lock);
        char **App = Hotkey->App;
        while(*App)
        {
            if(FocusedApp && StringsAreEqual(*App, FocusedApp))
            {
                pthread_mutex_unlock(&Lock);
                return false;
            }

            ++App;
        }

        pthread_mutex_unlock(&Lock);
        return true;
    }

    return true;
}

bool ExecuteHotkey(hotkey *Hotkey)
{
    bool Result = VerifyHotkeyType(Hotkey);
    if(Result)
    {
        Execute(Hotkey->Command);
        if(ActiveBindingMode->Prefix)
            UpdatePrefixTimer();
    }

    return Result;
}


void ActivateMode(const char *Mode)
{
    mode *BindingMode = GetBindingMode(Mode);
    if(BindingMode)
    {
        printf("Activate mode: %s\n", Mode);
        ActiveBindingMode = BindingMode;

        /* TODO(koekeishiya): Clean up 'Kwm' compatibility mode */
        if((Compatibility & (1 << 0)) &&
           (ActiveBindingMode->Color))
            ExecuteKwmBorderCommand();

        if(ActiveBindingMode->Prefix)
            UpdatePrefixTimer();
    }
}

mode *CreateBindingMode(const char *Mode)
{
    mode *Result = (mode *) malloc(sizeof(mode));
    memset(Result, 0, sizeof(mode));
    Result->Name = strdup(Mode);

    mode *Chain = GetLastBindingMode();
    Chain->Next = Result;
    return Result;
}

mode *GetLastBindingMode()
{
    mode *BindingMode = &DefaultBindingMode;
    while(BindingMode->Next)
        BindingMode = BindingMode->Next;

    return BindingMode;
}

mode *GetBindingMode(const char *Mode)
{
    mode *Result = NULL;

    mode *BindingMode = &DefaultBindingMode;
    while(BindingMode)
    {
        if(StringsAreEqual(BindingMode->Name, Mode))
        {
            Result = BindingMode;
            break;
        }

        BindingMode = BindingMode->Next;
    }

    return Result;
}

internal inline bool
CompareCmdKey(hotkey *A, hotkey *B)
{
    if(HasFlags(A, Hotkey_Flag_Cmd))
    {
        return (HasFlags(B, Hotkey_Flag_LCmd) ||
                HasFlags(B, Hotkey_Flag_RCmd) ||
                HasFlags(B, Hotkey_Flag_Cmd));
    }
    else
    {
        return ((HasFlags(A, Hotkey_Flag_LCmd) == HasFlags(B, Hotkey_Flag_LCmd)) &&
                (HasFlags(A, Hotkey_Flag_RCmd) == HasFlags(B, Hotkey_Flag_RCmd)) &&
                (HasFlags(A, Hotkey_Flag_Cmd) == HasFlags(B, Hotkey_Flag_Cmd)));
    }
}

internal inline bool
CompareShiftKey(hotkey *A, hotkey *B)
{
    if(HasFlags(A, Hotkey_Flag_Shift))
    {
        return (HasFlags(B, Hotkey_Flag_LShift) ||
                HasFlags(B, Hotkey_Flag_RShift) ||
                HasFlags(B, Hotkey_Flag_Shift));
    }
    else
    {
        return ((HasFlags(A, Hotkey_Flag_LShift) == HasFlags(B, Hotkey_Flag_LShift)) &&
                (HasFlags(A, Hotkey_Flag_RShift) == HasFlags(B, Hotkey_Flag_RShift)) &&
                (HasFlags(A, Hotkey_Flag_Shift) == HasFlags(B, Hotkey_Flag_Shift)));
    }
}

internal inline bool
CompareAltKey(hotkey *A, hotkey *B)
{
    if(HasFlags(A, Hotkey_Flag_Alt))
    {
        return (HasFlags(B, Hotkey_Flag_LAlt) ||
                HasFlags(B, Hotkey_Flag_RAlt) ||
                HasFlags(B, Hotkey_Flag_Alt));
    }
    else
    {
        return ((HasFlags(A, Hotkey_Flag_LAlt) == HasFlags(B, Hotkey_Flag_LAlt)) &&
                (HasFlags(A, Hotkey_Flag_RAlt) == HasFlags(B, Hotkey_Flag_RAlt)) &&
                (HasFlags(A, Hotkey_Flag_Alt) == HasFlags(B, Hotkey_Flag_Alt)));
    }
}

internal inline bool
CompareControlKey(hotkey *A, hotkey *B)
{
    if(HasFlags(A, Hotkey_Flag_Control))
    {
        return (HasFlags(B, Hotkey_Flag_LControl) ||
                HasFlags(B, Hotkey_Flag_RControl) ||
                HasFlags(B, Hotkey_Flag_Control));
    }
    else
    {
        return ((HasFlags(A, Hotkey_Flag_LControl) == HasFlags(B, Hotkey_Flag_LControl)) &&
                (HasFlags(A, Hotkey_Flag_RControl) == HasFlags(B, Hotkey_Flag_RControl)) &&
                (HasFlags(A, Hotkey_Flag_Control) == HasFlags(B, Hotkey_Flag_Control)));
    }
}

internal inline bool
HotkeysAreEqual(hotkey *A, hotkey *B)
{
    if(A && B)
    {
        if(HasFlags(A, Hotkey_Flag_Literal) != HasFlags(B, Hotkey_Flag_Literal))
            return false;

        if(!HasFlags(A, Hotkey_Flag_Literal) &&
           !HasFlags(B, Hotkey_Flag_Literal))
        {
            return CompareCmdKey(A, B) &&
                   CompareShiftKey(A, B) &&
                   CompareAltKey(A, B) &&
                   CompareControlKey(A, B);
        }
        else
        {
            return CompareCmdKey(A, B) &&
                   CompareShiftKey(A, B) &&
                   CompareAltKey(A, B) &&
                   CompareControlKey(A, B) &&
                   A->Key == B->Key;
        }
    }

    return false;
}

internal hotkey
CreateHotkeyFromCGEvent(CGEventFlags Flags, CGKeyCode Key)
{
    hotkey Eventkey = {};
    Eventkey.Key = Key;

    if((Flags & Event_Mask_Cmd) == Event_Mask_Cmd)
    {
        bool Left = (Flags & Event_Mask_LCmd) == Event_Mask_LCmd;
        bool Right = (Flags & Event_Mask_RCmd) == Event_Mask_RCmd;

        if(Left)
            AddFlags(&Eventkey, Hotkey_Flag_LCmd);

        if(Right)
            AddFlags(&Eventkey, Hotkey_Flag_RCmd);

        if(!Left && !Right)
            AddFlags(&Eventkey, Hotkey_Flag_Cmd);
    }

    if((Flags & Event_Mask_Shift) == Event_Mask_Shift)
    {
        bool Left = (Flags & Event_Mask_LShift) == Event_Mask_LShift;
        bool Right = (Flags & Event_Mask_RShift) == Event_Mask_RShift;

        if(Left)
            AddFlags(&Eventkey, Hotkey_Flag_LShift);

        if(Right)
            AddFlags(&Eventkey, Hotkey_Flag_RShift);

        if(!Left && !Right)
            AddFlags(&Eventkey, Hotkey_Flag_Shift);
    }

    if((Flags & Event_Mask_Alt) == Event_Mask_Alt)
    {
        bool Left = (Flags & Event_Mask_LAlt) == Event_Mask_LAlt;
        bool Right = (Flags & Event_Mask_RAlt) == Event_Mask_RAlt;

        if(Left)
            AddFlags(&Eventkey, Hotkey_Flag_LAlt);

        if(Right)
            AddFlags(&Eventkey, Hotkey_Flag_RAlt);

        if(!Left && !Right)
            AddFlags(&Eventkey, Hotkey_Flag_Alt);
    }

    if((Flags & Event_Mask_Control) == Event_Mask_Control)
    {
        bool Left = (Flags & Event_Mask_LControl) == Event_Mask_LControl;
        bool Right = (Flags & Event_Mask_RControl) == Event_Mask_RControl;

        if(Left)
            AddFlags(&Eventkey, Hotkey_Flag_LControl);

        if(Right)
            AddFlags(&Eventkey, Hotkey_Flag_RControl);

        if(!Left && !Right)
            AddFlags(&Eventkey, Hotkey_Flag_Control);
    }

    return Eventkey;
}

internal CGEventFlags
CreateCGEventFlagsFromHotkeyFlags(uint32_t HotkeyFlags)
{
    CGEventFlags EventFlags = 0;

    if(HotkeyFlags & Hotkey_Flag_Cmd)
        EventFlags |= Event_Mask_Cmd;
    else if(HotkeyFlags & Hotkey_Flag_LCmd)
        EventFlags |= Event_Mask_LCmd | Event_Mask_Cmd;
    else if(HotkeyFlags & Hotkey_Flag_RCmd)
        EventFlags |= Event_Mask_RCmd | Event_Mask_Cmd;

    if(HotkeyFlags & Hotkey_Flag_Shift)
        EventFlags |= Event_Mask_Shift;
    else if(HotkeyFlags & Hotkey_Flag_LShift)
        EventFlags |= Event_Mask_LShift | Event_Mask_Shift;
    else if(HotkeyFlags & Hotkey_Flag_RShift)
        EventFlags |= Event_Mask_RShift | Event_Mask_Shift;

    if(HotkeyFlags & Hotkey_Flag_Alt)
        EventFlags |= Event_Mask_Alt;
    else if(HotkeyFlags & Hotkey_Flag_LAlt)
        EventFlags |= Event_Mask_LAlt | Event_Mask_Alt;
    else if(HotkeyFlags & Hotkey_Flag_RAlt)
        EventFlags |= Event_Mask_RAlt | Event_Mask_Alt;

    if(HotkeyFlags & Hotkey_Flag_Control)
        EventFlags |= Event_Mask_Control;
    else if(HotkeyFlags & Hotkey_Flag_LControl)
        EventFlags |= Event_Mask_LControl | Event_Mask_Control;
    else if(HotkeyFlags & Hotkey_Flag_RControl)
        EventFlags |= Event_Mask_RControl | Event_Mask_Control;

    return EventFlags;
}

internal inline void
ExecuteModifierOnlyHotkey()
{
    CGEventFlags EventFlags = CreateCGEventFlagsFromHotkeyFlags(ModifierState.Flags);
    hotkey *Hotkey = NULL;
    if(HotkeyForCGEvent(EventFlags, 0, &Hotkey, false))
    {
        ExecuteHotkey(Hotkey);
    }
}

internal inline void
ModifierPressed(uint32_t Flag)
{
    ModifierState.Flags |= Flag;
    ClockGetTime(&ModifierState.Time);
    ModifierState.Valid = true;
}

internal inline void
ModifierReleased(uint32_t Flag)
{
    long long Time;
    ClockGetTime(&Time);

    if((GetTimeDiff(Time, ModifierState.Time) < ModifierState.Timeout) &&
       (ModifierState.Valid))
    {
        ExecuteModifierOnlyHotkey();
    }

    ModifierState.Flags &= ~Flag;
}

void RefreshModifierState(CGEventFlags Flags, CGKeyCode Key)
{
    if(Key == Modifier_Keycode_Left_Cmd)
    {
        if(Flags & Event_Mask_LCmd)
            ModifierPressed(Hotkey_Flag_LCmd);
        else
            ModifierReleased(Hotkey_Flag_LCmd);
    }
    else if(Key == Modifier_Keycode_Right_Cmd)
    {
        if(Flags & Event_Mask_RCmd)
            ModifierPressed(Hotkey_Flag_RCmd);
        else
            ModifierReleased(Hotkey_Flag_RCmd);
    }
    else if(Key == Modifier_Keycode_Left_Shift)
    {
        if(Flags & Event_Mask_LShift)
            ModifierPressed(Hotkey_Flag_LShift);
        else
            ModifierReleased(Hotkey_Flag_LShift);
    }
    else if(Key == Modifier_Keycode_Right_Shift)
    {
        if(Flags & Event_Mask_RShift)
            ModifierPressed(Hotkey_Flag_RShift);
        else
            ModifierReleased(Hotkey_Flag_RShift);
    }
    else if(Key == Modifier_Keycode_Left_Alt)
    {
        if(Flags & Event_Mask_LAlt)
            ModifierPressed(Hotkey_Flag_LAlt);
        else
            ModifierReleased(Hotkey_Flag_LAlt);
    }
    else if(Key == Modifier_Keycode_Right_Alt)
    {
        if(Flags & Event_Mask_RAlt)
            ModifierPressed(Hotkey_Flag_RAlt);
        else
            ModifierReleased(Hotkey_Flag_RAlt);
    }
    else if(Key == Modifier_Keycode_Left_Ctrl)
    {
        if(Flags & Event_Mask_LControl)
            ModifierPressed(Hotkey_Flag_LControl);
        else
            ModifierReleased(Hotkey_Flag_LControl);
    }
    else if(Key == Modifier_Keycode_Right_Ctrl)
    {
        if(Flags & Event_Mask_RControl)
            ModifierPressed(Hotkey_Flag_RControl);
        else
            ModifierReleased(Hotkey_Flag_RControl);
    }
}

internal bool
HotkeyExists(uint32_t Flags, CGKeyCode Keycode, hotkey **Result, const char *Mode)
{
    hotkey TempHotkey = {};
    TempHotkey.Flags = Flags;
    TempHotkey.Key = Keycode;

    mode *BindingMode = GetBindingMode(Mode);
    if(BindingMode)
    {
        hotkey *Hotkey = BindingMode->Hotkey;
        while(Hotkey)
        {
            if(HotkeysAreEqual(Hotkey, &TempHotkey))
            {
                *Result = Hotkey;
                return true;
            }

            Hotkey = Hotkey->Next;
        }
    }

    return false;
}

bool HotkeyForCGEvent(CGEventFlags Flags, CGKeyCode Key, hotkey **Hotkey, bool Literal)
{
    hotkey Eventkey = CreateHotkeyFromCGEvent(Flags, Key);
    if(Literal)
    {
        AddFlags(&Eventkey, Hotkey_Flag_Literal);
    }

    return HotkeyExists(Eventkey.Flags, Eventkey.Key, Hotkey, ActiveBindingMode->Name);
}

void SendKeySequence(const char *Sequence)
{
    CFStringRef SequenceRef = CFStringCreateWithCString(NULL, Sequence, kCFStringEncodingMacRoman);
    CGEventRef KeyDownEvent = CGEventCreateKeyboardEvent(NULL, 0, true);
    CGEventRef KeyUpEvent = CGEventCreateKeyboardEvent(NULL, 0, false);

    UniChar OutputBuffer;
    for(size_t Index = 0;
        *Sequence != '\0';
        ++Index, ++Sequence)
    {
        CFStringGetCharacters(SequenceRef, CFRangeMake(Index, 1), &OutputBuffer);

        CGEventSetFlags(KeyDownEvent, 0);
        CGEventKeyboardSetUnicodeString(KeyDownEvent, 1, &OutputBuffer);
        CGEventPost(kCGHIDEventTap, KeyDownEvent);

        CGEventSetFlags(KeyUpEvent, 0);
        CGEventKeyboardSetUnicodeString(KeyUpEvent, 1, &OutputBuffer);
        CGEventPost(kCGHIDEventTap, KeyUpEvent);

        usleep(100);
    }

    CFRelease(KeyUpEvent);
    CFRelease(KeyDownEvent);
    CFRelease(SequenceRef);
}

internal inline void
CreateAndPostKeyEvent(uint32_t Flags, CGKeyCode Key, bool Pressed)
{
    CGEventRef KeyEvent = CGEventCreateKeyboardEvent(NULL, Key, Pressed);

    CGEventSetFlags(KeyEvent, Flags | CGEventGetFlags(KeyEvent));
    CGEventPost(kCGHIDEventTap, KeyEvent);

    CFRelease(KeyEvent);
    usleep(10000);
}

internal inline void
CreateAndPostKeyEvent(CGKeyCode Key, bool Pressed)
{
    CreateAndPostKeyEvent(0, Key, Pressed);
}

/* NOTE(koekeishiya): Simply setting the modifier flag for an event is apparently
 * no longer enough when using CGPostEvent on MacOS Sierra. This function performs
 * a key-pressed event for each active modifier. */
internal void
PressModifiers(uint32_t HotkeyFlags)
{
    if(HotkeyFlags & Hotkey_Flag_Cmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Cmd, true);
    else if(HotkeyFlags & Hotkey_Flag_LCmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Cmd, true);
    else if(HotkeyFlags & Hotkey_Flag_RCmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Cmd, true);

    if(HotkeyFlags & Hotkey_Flag_Shift)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Shift, true);
    else if(HotkeyFlags & Hotkey_Flag_LShift)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Shift, true);
    else if(HotkeyFlags & Hotkey_Flag_RShift)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Shift, true);

    if(HotkeyFlags & Hotkey_Flag_Alt)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Alt, true);
    else if(HotkeyFlags & Hotkey_Flag_LAlt)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Alt, true);
    else if(HotkeyFlags & Hotkey_Flag_RAlt)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Alt, true);

    if(HotkeyFlags & Hotkey_Flag_Control)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Ctrl, true);
    else if(HotkeyFlags & Hotkey_Flag_LControl)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Ctrl, true);
    else if(HotkeyFlags & Hotkey_Flag_RControl)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Ctrl, true);
}

/* NOTE(koekeishiya): Simply setting the modifier flag for an event is apparently
 * no longer enough when using CGPostEvent on MacOS Sierra. This function performs
 * a key-release event for each active modifier. */
internal void
ReleaseModifiers(uint32_t HotkeyFlags)
{
    if(HotkeyFlags & Hotkey_Flag_Cmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Cmd, false);
    else if(HotkeyFlags & Hotkey_Flag_LCmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Cmd, false);
    else if(HotkeyFlags & Hotkey_Flag_RCmd)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Cmd, false);

    if(HotkeyFlags & Hotkey_Flag_Shift)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Shift, false);
    else if(HotkeyFlags & Hotkey_Flag_LShift)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Shift, false);
    else if(HotkeyFlags & Hotkey_Flag_RShift)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Shift, false);

    if(HotkeyFlags & Hotkey_Flag_Alt)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Alt, false);
    else if(HotkeyFlags & Hotkey_Flag_LAlt)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Alt, false);
    else if(HotkeyFlags & Hotkey_Flag_RAlt)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Alt, false);

    if(HotkeyFlags & Hotkey_Flag_Control)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Ctrl, false);
    else if(HotkeyFlags & Hotkey_Flag_LControl)
        CreateAndPostKeyEvent(Modifier_Keycode_Left_Ctrl, false);
    else if(HotkeyFlags & Hotkey_Flag_RControl)
        CreateAndPostKeyEvent(Modifier_Keycode_Right_Ctrl, false);
}

void SendKeyPress(char *KeySym)
{
    hotkey Hotkey = {};
    ParseKeySym(KeySym, &Hotkey);
    CGEventFlags Flags = CreateCGEventFlagsFromHotkeyFlags(Hotkey.Flags);

    // TODO(koekeishiya): Not necessary on El Capitan! Is this a Sierra thing ?
    PressModifiers(Hotkey.Flags);

    CreateAndPostKeyEvent(Flags, Hotkey.Key, true);
    CreateAndPostKeyEvent(Flags, Hotkey.Key, false);

    // TODO(koekeishiya): Not necessary on El Capitan! Is this a Sierra thing ?
    ReleaseModifiers(Hotkey.Flags);
}
