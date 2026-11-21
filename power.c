#include "power.h"
#include <powrprof.h>

static const wchar_t recovery_key[] = L"Software\\DisplayOff";
static const wchar_t recovery_value[] = L"SavedTimeouts";
static const DWORD temporary_timeout = 1;

typedef struct {
    GUID scheme;
    DWORD ac;
    DWORD dc;
} DisplayTimeouts;

static DWORD active_scheme(GUID *scheme)
{
    GUID *active = NULL;
    DWORD status = PowerGetActiveScheme(NULL, &active);
    if (status == ERROR_SUCCESS) {
        *scheme = *active;
        LocalFree(active);
    }
    return status;
}

static DWORD read_timeouts(DisplayTimeouts *timeouts)
{
    DWORD status = PowerReadACValueIndex(NULL, &timeouts->scheme,
        &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, &timeouts->ac);
    if (status != ERROR_SUCCESS)
        return status;
    return PowerReadDCValueIndex(NULL, &timeouts->scheme,
        &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, &timeouts->dc);
}

static DWORD save_timeouts(const DisplayTimeouts *timeouts)
{
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, recovery_key, 0, NULL,
        0, KEY_SET_VALUE, NULL, &key, NULL);
    if (status != ERROR_SUCCESS)
        return status;
    status = RegSetValueExW(key, recovery_value, 0, REG_BINARY,
        (const BYTE *)timeouts, sizeof *timeouts);
    if (status == ERROR_SUCCESS)
        status = RegFlushKey(key);
    RegCloseKey(key);
    return status;
}

static DWORD apply_if_active(const GUID *scheme)
{
    GUID active;
    DWORD status = active_scheme(&active);
    if (status == ERROR_SUCCESS && IsEqualGUID(scheme, &active))
        status = PowerSetActiveScheme(NULL, scheme);
    return status;
}

DWORD displayoff_restore(void)
{
    DisplayTimeouts saved;
    DWORD size = sizeof saved;
    DWORD status = RegGetValueW(HKEY_CURRENT_USER, recovery_key,
        recovery_value, RRF_RT_REG_BINARY, NULL, &saved, &size);
    if (status == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS)
        return status;
    if (size != sizeof saved)
        return ERROR_INVALID_DATA;

    DisplayTimeouts current = { .scheme = saved.scheme };
    status = read_timeouts(&current);
    if (status != ERROR_SUCCESS)
        return status;

    // Restore the original plan and leave any intervening timeout edits alone.
    DWORD ac = current.ac == temporary_timeout ? saved.ac : current.ac;
    DWORD dc = current.dc == temporary_timeout ? saved.dc : current.dc;
    if (ac != current.ac) {
        status = PowerWriteACValueIndex(NULL, &saved.scheme,
            &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, ac);
        if (status != ERROR_SUCCESS)
            return status;
    }
    if (dc != current.dc) {
        status = PowerWriteDCValueIndex(NULL, &saved.scheme,
            &GUID_VIDEO_SUBGROUP, &GUID_VIDEO_POWERDOWN_TIMEOUT, dc);
        if (status != ERROR_SUCCESS)
            return status;
    }

    status = apply_if_active(&saved.scheme);
    if (status != ERROR_SUCCESS)
        return status;
    status = read_timeouts(&current);
    if (status != ERROR_SUCCESS)
        return status;
    if (current.ac != ac || current.dc != dc)
        return ERROR_WRITE_FAULT;
    return RegDeleteKeyValueW(HKEY_CURRENT_USER, recovery_key, recovery_value);
}

DWORD displayoff_begin(void)
{
    DWORD status = displayoff_restore();
    if (status != ERROR_SUCCESS)
        return status;

    DisplayTimeouts saved;
    status = active_scheme(&saved.scheme);
    if (status == ERROR_SUCCESS)
        status = read_timeouts(&saved);
    if (status == ERROR_SUCCESS)
        status = save_timeouts(&saved);
    if (status != ERROR_SUCCESS)
        return status;

    status = PowerWriteACValueIndex(NULL, &saved.scheme, &GUID_VIDEO_SUBGROUP,
        &GUID_VIDEO_POWERDOWN_TIMEOUT, temporary_timeout);
    if (status == ERROR_SUCCESS)
        status = PowerWriteDCValueIndex(NULL, &saved.scheme, &GUID_VIDEO_SUBGROUP,
            &GUID_VIDEO_POWERDOWN_TIMEOUT, temporary_timeout);
    if (status == ERROR_SUCCESS)
        status = apply_if_active(&saved.scheme);
    return status;
}
