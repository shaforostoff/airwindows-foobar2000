/* ========================================
 *  foo_dsp_decrackle - configuration dialog
 *
 *  Deliberately plain Win32: no ATL, no WTL, no libPPUI, so the component
 *  builds from nothing but the foobar2000 SDK archive. The trade-off is that
 *  the dialog does not follow foobar2000 v2's dark mode.
 * ======================================== */

#include "stdafx.h"

#include "decrackle_preset.h"
#include "resource.h"

#include <stddef.h>
#include <stdio.h>

using airwindows::DeCrackleParams;

namespace {

enum { kSliderRange = 1000, kSliderPage = 50, kSliderLine = 5 };

struct slider_def {
    int    sliderId;
    int    valueId;
    size_t offset;      // byte offset of the float inside DeCrackleParams
};

const slider_def k_sliders[] = {
    { IDC_SLIDER_FILTER,    IDC_VALUE_FILTER,    offsetof(DeCrackleParams, filter)    },
    { IDC_SLIDER_WINDOW,    IDC_VALUE_WINDOW,    offsetof(DeCrackleParams, window)    },
    { IDC_SLIDER_THRESHOLD, IDC_VALUE_THRESHOLD, offsetof(DeCrackleParams, threshold) },
    { IDC_SLIDER_SURFACE,   IDC_VALUE_SURFACE,   offsetof(DeCrackleParams, surface)   },
    { IDC_SLIDER_DRYWET,    IDC_VALUE_DRYWET,    offsetof(DeCrackleParams, dryWet)    },
};

const size_t k_sliderCount = sizeof(k_sliders) / sizeof(k_sliders[0]);

float * param_field(DeCrackleParams & p, size_t offset) {
    return (float *)((char *)&p + offset);
}

struct dialog_state {
    dsp_preset_edit_callback * callback;
    DeCrackleParams            params;
};

void refresh_value_label(HWND dlg, int valueId, float value) {
    wchar_t text[32];
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%.3f", (double)value);
    SetDlgItemTextW(dlg, valueId, text);
}

void push_preset(dialog_state * st) {
    dsp_preset_impl preset;
    decrackle_preset::make(st->params, preset);
    st->callback->on_preset_changed(preset);
}

void load_into_controls(HWND dlg, dialog_state * st) {
    for (size_t i = 0; i < k_sliderCount; ++i) {
        const float v = *param_field(st->params, k_sliders[i].offset);
        const int pos = (int)(v * (float)kSliderRange + 0.5f);
        SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETPOS, TRUE, (LPARAM)pos);
        refresh_value_label(dlg, k_sliders[i].valueId, v);
    }
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    dialog_state * st = (dialog_state *)GetWindowLongPtr(dlg, GWLP_USERDATA);

    switch (msg) {
    case WM_INITDIALOG:
        st = (dialog_state *)lp;
        SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)st);
        for (size_t i = 0; i < k_sliderCount; ++i) {
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETRANGE, FALSE,
                               MAKELPARAM(0, kSliderRange));
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETPAGESIZE, 0,
                               (LPARAM)kSliderPage);
            SendDlgItemMessage(dlg, k_sliders[i].sliderId, TBM_SETLINESIZE, 0,
                               (LPARAM)kSliderLine);
        }
        load_into_controls(dlg, st);
        return TRUE;

    case WM_HSCROLL: {
        if (st == NULL || lp == 0) break;
        const HWND bar = (HWND)lp;
        const int  id  = GetDlgCtrlID(bar);
        for (size_t i = 0; i < k_sliderCount; ++i) {
            if (k_sliders[i].sliderId != id) continue;
            const LRESULT pos = SendMessage(bar, TBM_GETPOS, 0, 0);
            const float v = (float)((double)pos / (double)kSliderRange);
            float * field = param_field(st->params, k_sliders[i].offset);
            if (*field != v) {
                *field = v;
                refresh_value_label(dlg, k_sliders[i].valueId, v);
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
                st->params = DeCrackleParams::defaults();
                load_into_controls(dlg, st);
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

void decrackle_config_popup(const dsp_preset & data, HWND parent,
                            dsp_preset_edit_callback & callback) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    dialog_state st;
    st.callback = &callback;
    st.params   = decrackle_preset::parse(data);

    const INT_PTR result = DialogBoxParam(core_api::get_my_instance(),
                                          MAKEINTRESOURCE(IDD_DECRACKLE),
                                          parent, dialog_proc, (LPARAM)&st);

    if (result != IDOK) {
        // Anything other than OK - including a failure to create the dialog -
        // puts the preset we were handed back where it was.
        callback.on_preset_changed(data);
    }
}
