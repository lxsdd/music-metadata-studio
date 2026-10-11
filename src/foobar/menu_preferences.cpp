#include "stdafx.h"

#include <SDK/coreDarkMode.h>

#include "menu_settings.h"
#include "resource.h"

#include <SDK/preferences_page.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace djmeta_foobar {
namespace {

constexpr std::array<int, menu_caption_count> kControlIDs = {
    IDC_CAPTION_METADATA, IDC_CAPTION_SINGLES, IDC_CAPTION_ALBEN,
    IDC_CAPTION_LIVESETS, IDC_CAPTION_PREPARE
};

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("menu label too long");
    const int length = static_cast<int>(text.size());
    const int wide_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0);
    if (wide_chars <= 0) throw std::invalid_argument("invalid saved UTF-8 menu label");
    std::wstring result(static_cast<std::size_t>(wide_chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), length, result.data(), wide_chars) != wide_chars)
        throw std::runtime_error("unable to load menu label");
    return result;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size()); // control limit <= 160
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::invalid_argument("invalid Unicode menu label");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, result.data(), bytes, nullptr, nullptr) != bytes)
        throw std::runtime_error("unable to save menu label");
    return result;
}

std::wstring read_control(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    if (!control) throw std::runtime_error("missing menu caption control");
    const int length = GetWindowTextLengthW(control);
    if (length < 0 || length > 160)
        throw std::invalid_argument("menu label length exceeds 160 characters");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const int n = GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(n));
    return value;
}

bool acceptable_caption(const std::wstring& caption) {
    if (caption.empty() || caption.size() > 160) return false;
    for (const wchar_t ch : caption)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

constexpr GUID kPreferencesGuid =
    {0x728f3851, 0xbe84, 0x432d, {0xab, 0x07, 0x34, 0xa2, 0xd9, 0x18, 0xe5, 0x0a}};

class MenuPreferencesInstance : public preferences_page_instance {
public:
    MenuPreferencesInstance(fb2k::hwnd_t parent, preferences_page_callback::ptr callback)
        : m_callback(std::move(callback)) {
        m_window = CreateDialogParamW(core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDD_MENU_PREFERENCES), parent,
            dialog_proc, reinterpret_cast<LPARAM>(this));
        if (!m_window)
            throw std::runtime_error("Failed to create Music Metadata Studio Preferences");
    }

    ~MenuPreferencesInstance() {
        if (m_window && IsWindow(m_window)) DestroyWindow(m_window);
    }

    fb2k::hwnd_t get_wnd() override { return m_window; }

    t_uint32 get_state() override {
        t_uint32 flags = preferences_state::resettable |
                         preferences_state::dark_mode_supported;
        try {
            for (unsigned index = 0; index < menu_caption_count; ++index) {
                const auto typed = wide_to_utf8(read_control(m_window, kControlIDs[index]));
                if (typed != effective_menu_caption(index)) {
                    flags |= preferences_state::changed;
                    break;
                }
            }
        } catch (const std::exception&) {
            flags |= preferences_state::changed;
        }
        return flags;
    }

    void apply() override {
        std::array<std::string, menu_caption_count> all;
        try {
            for (unsigned index = 0; index < menu_caption_count; ++index) {
                const std::wstring value = read_control(m_window, kControlIDs[index]);
                if (!acceptable_caption(value))
                    throw std::invalid_argument("Empty or invalid menu caption.");
                all[index] = wide_to_utf8(value);
            }
        } catch (const std::exception&) {
            MessageBoxW(m_window,
                L"All five menu captions must contain 1 to 160 valid characters. "
                L"No settings were changed.",
                L"Music Metadata Studio", MB_OK | MB_ICONWARNING);
            return;
        }

        // Validate the WHOLE form before changing any foobar cfg value.
        for (unsigned index = 0; index < menu_caption_count; ++index)
            set_menu_caption(index, all[index].c_str());
        state_changed();
    }

    void reset() override {
        for (unsigned index = 0; index < menu_caption_count; ++index) {
            const auto text = utf8_to_wide(default_menu_caption(index));
            SetDlgItemTextW(m_window, kControlIDs[index], text.c_str());
        }
        state_changed(); // Reset only stages defaults; Apply persists.
    }

private:
    static INT_PTR CALLBACK dialog_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<MenuPreferencesInstance*>(lp);
            self->m_window = window; // WM_INITDIALOG precedes CreateDialogParamW returning.
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_dark.AddDialogWithControls(window);
            try {
                for (unsigned i = 0; i < menu_caption_count; ++i) {
                    SendDlgItemMessageW(window, kControlIDs[i], EM_LIMITTEXT, 160, 0);
                    const auto caption = utf8_to_wide(effective_menu_caption(i));
                    SetDlgItemTextW(window, kControlIDs[i], caption.c_str());
                }
            } catch (const std::exception&) {
                // Invalid or unavailable saved settings are not overwritten
                // until the user explicitly presses Apply.
                for (unsigned i = 0; i < menu_caption_count; ++i) {
                    const auto caption = utf8_to_wide(default_menu_caption(i));
                    SetDlgItemTextW(window, kControlIDs[i], caption.c_str());
                }
            }
            return TRUE;
        }
        auto* self = reinterpret_cast<MenuPreferencesInstance*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return FALSE;
        if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
            self->state_changed();
            return TRUE;
        }
        if (msg == WM_NCDESTROY) {
            self->m_window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return FALSE;
    }

    void state_changed() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    preferences_page_callback::ptr m_callback;
    fb2k::hwnd_t m_window = nullptr;
    fb2k::CCoreDarkModeHooks m_dark;
};

class MenuPreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Music Metadata Studio"; }
    GUID get_guid() override { return kPreferencesGuid; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
    preferences_page_instance::ptr instantiate(
        fb2k::hwnd_t parent,
        preferences_page_callback::ptr callback) override {
        return fb2k::service_new<MenuPreferencesInstance>(parent, std::move(callback));
    }
};

preferences_page_factory_t<MenuPreferencesPage> g_menu_preferences_factory;

} // namespace
} // namespace djmeta_foobar
