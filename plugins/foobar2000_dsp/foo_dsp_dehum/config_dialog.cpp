/* ========================================
 *  foo_dsp_dehum - configuration dialog
 *
 *  Plain Win32, same as the other two components: no ATL, no WTL, no libPPUI,
 *  so the component builds from nothing but the foobar2000 SDK archive.
 * ======================================== */

#include "stdafx.h"

#include "dehum_preset.h"
#include "resource.h"

#include <stdio.h>

using dehum::Params;

namespace {

//! Each slider works in integer steps and maps onto its parameter through
//! `lo + pos * step`. Keeping the mapping in a table lets one handler drive all
//! seven of them.
//!
//! Two of the parameters have an off position rather than a range that reaches
//! zero - Frequency is automatic at 0 and pinned from 10 Hz up, Rumble is off at
//! 0 and a corner frequency from 10 Hz up - so `offText` marks a slider whose
//! bottom position means that instead of `lo`.
struct slider_def {
    int    sliderId;
    int    valueId;
    int    steps;        // slider range is 0..steps
    double lo, hi;       // parameter range
    int    decimals;     // for the readout
    const wchar_t * suffix;
    const wchar_t * offText;   // NULL if the slider has no off position
};

const slider_def k_sliders[] = {
    { IDC_SLIDER_SENSITIVITY, IDC_VALUE_SENSITIVITY, 1000,  0.0,   1.0, 3, L"",     NULL      },
    { IDC_SLIDER_BANDWIDTH,   IDC_VALUE_BANDWIDTH,    490,  0.1,   5.0, 2, L" Hz",  NULL      },
    { IDC_SLIDER_SEARCHTO,    IDC_VALUE_SEARCHTO,     460, 40.0, 500.0, 0, L" Hz",  NULL      },
    { IDC_SLIDER_HARMONICS,   IDC_VALUE_HARMONICS,      7,  1.0,   8.0, 0, L"",     NULL      },
    { IDC_SLIDER_FREQUENCY,   IDC_VALUE_FREQUENCY,   4901, 10.0, 500.0, 1, L" Hz",  L"auto"   },
    { IDC_SLIDER_RUMBLE,      IDC_VALUE_RUMBLE,       191, 10.0, 200.0, 0, L" Hz",  L"off"    },
    { IDC_SLIDER_DRYWET,      IDC_VALUE_DRYWET,      1000,  0.0,   1.0, 3, L"",     NULL      },
};

const size_t k_count = sizeof(k_sliders) / sizeof(k_sliders[0]);

double get_param(const Params & p, size_t i) {
    switch (i) {
    case 0: return p.sensitivity;
    case 1: return p.bandwidth;
    case 2: return p.searchTo;
    case 3: return (double)p.harmonics;
    case 4: return p.frequency;
    case 5: return p.rumbleHz;
    default: return p.dryWet;
    }
}

void set_param(Params & p, size_t i, double v) {
    switch (i) {
    case 0: p.sensitivity = (float)v; break;
    case 1: p.bandwidth = (float)v; break;
    case 2: p.searchTo = (float)v; break;
    case 3: p.harmonics = (int)(v + 0.5); break;
    case 4: p.frequency = (float)v; break;
    case 5: p.rumbleHz = (float)v; break;
    default: p.dryWet = (float)v; break;
    }
}

double pos_to_value(const slider_def & s, int pos) {
    if (s.steps <= 0) return s.lo;
    if (s.offText != NULL) {
        if (pos <= 0) return 0.0;
        return s.lo + (s.hi - s.lo) * ((double)(pos - 1) / (double)(s.steps - 1));
    }
    return s.lo + (s.hi - s.lo) * ((double)pos / (double)s.steps);
}

int value_to_pos(const slider_def & s, double v) {
    if (s.hi <= s.lo) return 0;
    if (s.offText != NULL) {
        if (v <= 0.0) return 0;
        double t = (v - s.lo) / (s.hi - s.lo);
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return 1 + (int)(t * (double)(s.steps - 1) + 0.5);
    }
    double t = (v - s.lo) / (s.hi - s.lo);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return (int)(t * (double)s.steps + 0.5);
}

struct dialog_state {
    dsp_preset_edit_callback * callback;
    Params                     params;
};

void refresh_label(HWND dlg, const slider_def & s, double v) {
    wchar_t text[48];
    if (s.offText != NULL && v <= 0.0) {
        SetDlgItemTextW(dlg, s.valueId, s.offText);
        return;
    }
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%.*f%s", s.decimals, v, s.suffix);
    SetDlgItemTextW(dlg, s.valueId, text);
}

void push_preset(dialog_state * st) {
    dsp_preset_impl preset;
    dehum_preset::make(st->params, preset);
    st->callback->on_preset_changed(preset);
}

void load_controls(HWND dlg, dialog_state * st) {
    for (size_t i = 0; i < k_count; ++i) {
        const double v = get_param(st->params, i);
        SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETPOS, TRUE,
                           (LPARAM)value_to_pos(k_sliders[i], v));
        refresh_label(dlg, k_sliders[i], v);
    }
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    dialog_state * st = (dialog_state *)GetWindowLongPtr(dlg, GWLP_USERDATA);

    switch (msg) {
    case WM_INITDIALOG:
        st = (dialog_state *)lp;
        SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)st);
        for (size_t i = 0; i < k_count; ++i) {
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETRANGE, FALSE,
                               MAKELPARAM(0, k_sliders[i].steps));
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETPAGESIZE, 0,
                               (LPARAM)(k_sliders[i].steps > 20
                                        ? k_sliders[i].steps / 20 : 1));
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETLINESIZE, 0, 1);
        }
        load_controls(dlg, st);
        return TRUE;

    case WM_HSCROLL: {
        if (st == NULL || lp == 0) break;
        const HWND bar = (HWND)lp;
        const int id = GetDlgCtrlID(bar);
        for (size_t i = 0; i < k_count; ++i) {
            if (k_sliders[i].sliderId != id) continue;
            const int pos = (int)SendMessage(bar, TBM_GETPOS, 0, 0);
            const double v = pos_to_value(k_sliders[i], pos);
            if (get_param(st->params, i) != v) {
                set_param(st->params, i, v);
                refresh_label(dlg, k_sliders[i], v);
                push_preset(st);
            }
            break;
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (st == NULL) break;
        switch (LOWORD(wp)) {
        case IDC_DEFAULTS:
            if (HIWORD(wp) == BN_CLICKED) {
                st->params = Params::defaults();
                load_controls(dlg, st);
                push_preset(st);
                return TRUE;
            }
            break;
        case IDOK:
            if (HIWORD(wp) == BN_CLICKED) { EndDialog(dlg, IDOK); return TRUE; }
            break;
        case IDCANCEL:
            if (HIWORD(wp) == BN_CLICKED) { EndDialog(dlg, IDCANCEL); return TRUE; }
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

} // anonymous namespace

void dehum_config_popup(const dsp_preset & data, HWND parent,
                        dsp_preset_edit_callback & callback) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    dialog_state st;
    st.callback = &callback;
    st.params = dehum_preset::parse(data);

    const INT_PTR result = DialogBoxParam(core_api::get_my_instance(),
                                          MAKEINTRESOURCE(IDD_DEHUM),
                                          parent, dialog_proc, (LPARAM)&st);
    if (result != IDOK) {
        callback.on_preset_changed(data);
    }
}
